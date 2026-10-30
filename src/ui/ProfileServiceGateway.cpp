#include "ui/ProfileServiceGateway.h"
#include "files/ProfileService.h"
#include "kbd/KeyboardDevice.h"
#include "kbd/KeychronProtocol.h"
#include "kbd/ProfileMemory.h"

#include <algorithm>
#include <cctype>
#include <span>
#include <string_view>

namespace ui {

namespace {

    using kbd::KeychronProtocol;

    constexpr std::chrono::milliseconds ACK_TIMEOUT{600};
    constexpr int                       READ_SLICE_MS = 100;

    // Reply layout of a raw-profile read (AMC_GET_PROFILE_RAW):
    // [0]=0xA9 [1]=0x12 [2]=slot [3..4]=offset [5]=size [6..]=data
    constexpr std::size_t PROFILE_READ_DATA_OFFSET = 6;

    bool containsIgnoreCase(const std::string& text, const std::string_view needle) {
        const auto it = std::search(text.begin(), text.end(), needle.begin(), needle.end(),
                                    [](const char a, const char b) {
                                        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                                    });
        return it != text.end();
    }

    // Actuation settings stored in a profile file's 860-byte memory dump, if it has a readable one.
    std::optional<ActuationSettings> decodeStoredSettings(const KeyboardProfile& profile) {
        try {
            const auto memory = KeychronProtocol::extractMemoryBuffer(profile.m_configData);
            const auto config = kbd::ProfileMemory::decodeGlobalTravel(memory);
            if (config) return actuation::fromGlobalTravel(*config);
        } catch (const std::exception&) {
            return std::nullopt; // the file has no (valid) memory dump: show the profile without actuation details
        }
        return std::nullopt;
    }

    std::uint8_t toSlot(const std::uint8_t slot) {
        return static_cast<std::uint8_t>(slot % SLOT_COUNT);
    }

} // namespace

ProfileServiceGateway::ProfileServiceGateway(kbd::ProfileService& service, kbd::KeyboardDevice& keyboard)
    : m_service(service), m_keyboard(keyboard)
{}

ProfileServiceGateway::~ProfileServiceGateway() = default;

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

DeviceInfo ProfileServiceGateway::describeDevice() const {
    DeviceInfo info;
    if (m_keyboard.isDryRun()) {
        info.connected = true;
        info.transport = kbd::TransportMode::Wired;
        info.path      = "(dry run)";
        return info;
    }

    info.connected = m_keyboard.isConnected();
    if (!info.connected) return info;

    info.path = m_keyboard.getDevicePath();
    // Same rule as kbd::DeviceWatcher: the wired keyboard (PID 0x0E21) shows up in its HID path.
    info.transport = containsIgnoreCase(info.path, "0e21") ? kbd::TransportMode::Wired : kbd::TransportMode::WirelessDongle;
    return info;
}

std::array<SlotInfo, SLOT_COUNT> ProfileServiceGateway::describeSlots() const {
    std::array<SlotInfo, SLOT_COUNT> slots;

    for (std::size_t i = 0; i < SLOT_COUNT; ++i) {
        SlotInfo& info = slots[i];
        info.name = slotLabel(i);

        const auto profile = m_service.getProfileForSlot(static_cast<std::uint8_t>(i));
        if (!profile) continue;

        info.hasProfile     = true;
        info.name           = profile->m_name;
        info.fileName       = profile->m_filePath.filename().string();
        info.storedSettings = decodeStoredSettings(*profile);
    }
    return slots;
}

// ---------------------------------------------------------------------------
// Keyboard round-trips
// ---------------------------------------------------------------------------

int ProfileServiceGateway::queryActiveSlot() {
    return m_service.queryActiveHardwareSlot();
}

std::optional<std::vector<std::uint8_t>> ProfileServiceGateway::awaitResponse(const std::uint8_t command,
                                                                              const std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::vector<std::uint8_t> report;

    while (std::chrono::steady_clock::now() < deadline) {
        const int bytes = m_keyboard.readReport(report, READ_SLICE_MS);
        if (bytes < 0) return std::nullopt; // the device went away

        if (bytes >= 3 && report[0] == KeychronProtocol::CMD_ANALOG_MATRIX && report[1] == command) {
            return report;
        }
    }
    return std::nullopt;
}

CommandResult ProfileServiceGateway::sendAcknowledged(const std::vector<std::uint8_t>& packet, const std::uint8_t command,
                                                      const std::string& what) {
    if (!describeDevice().connected) return {false, "The keyboard is not connected."};

    m_keyboard.flushInputBuffer();
    if (!m_keyboard.sendReport(packet)) return {false, "Could not send the " + what + " command to the keyboard."};

    if (m_keyboard.isDryRun()) return {true, what + " sent (dry run)."};

    const auto reply = awaitResponse(command, ACK_TIMEOUT);
    if (!reply) return {false, "The keyboard did not acknowledge the " + what + " command."};

    // The firmware answers with 0 on success and 1 on failure (analog_matrix.c: data[2] = success ? 0 : 1).
    if ((*reply)[2] != 0) return {false, "The keyboard rejected the " + what + " command."};
    return {true, what + " accepted."};
}

CommandResult ProfileServiceGateway::activateSlot(const std::uint8_t slot) {
    const std::uint8_t target = toSlot(slot);

    if (!m_service.switchHardwareSlot(target)) {
        return {false, "The keyboard did not confirm the switch to " + slotLabel(target) + "."};
    }

    const auto profile = m_service.getProfileForSlot(target);
    return {true, slotLabel(target) + " is active" + (profile ? " (" + profile->m_name + ")." : ".")};
}

CommandResult ProfileServiceGateway::cycleProfile(const bool forward) {
    if (m_service.profileCount() == 0) return {false, "No profile files found in the profiles folder."};

    if (forward) {
        m_service.cycleForward();
    } else {
        m_service.cycleBackward();
    }

    const int active = m_service.queryActiveHardwareSlot();
    if (active < 0) return {false, "The keyboard did not respond after switching profiles."};

    const auto slot = static_cast<std::size_t>(active);
    const auto profile = m_service.getProfileForSlot(static_cast<std::uint8_t>(active));
    if (!profile) return {true, "Switched to " + slotLabel(slot) + "."};
    return {true, "Switched to " + profile->m_name + " (" + slotLabel(slot) + ")."};
}

CommandResult ProfileServiceGateway::applyActuation(const std::uint8_t slot, const ActuationSettings& requested) {
    const std::uint8_t target = toSlot(slot);
    const ActuationSettings settings = actuation::sanitized(requested);

    if (!describeDevice().connected) return {false, "The keyboard is not connected."};

    m_keyboard.flushInputBuffer();
    if (!m_service.applyFastTravel(target, settings.rapidTrigger, settings.actuationTenths,
                                  settings.pressTenths, settings.releaseTenths)) {
        return {false, "Could not send the actuation command to the keyboard."};
    }

    if (!m_keyboard.isDryRun()) {
        const auto reply = awaitResponse(KeychronProtocol::AMC_SET_TRAVEL, ACK_TIMEOUT);
        if (!reply) return {false, "The keyboard did not acknowledge the actuation command."};
        if ((*reply)[2] != 0) return {false, "The keyboard rejected the actuation settings."};
    }

    // The firmware writes a "whole keyboard" setting into the slot's RAM profile but only rebuilds the
    // per-key tables it actually scans with when the ACTIVE profile changes (analog_matrix/profile.c:
    // profile_set_traval() -> no update_travel_configs() on the global path). If we just edited the active
    // slot, hop to a neighbouring slot and back so the new values take effect immediately.
    if (m_service.queryActiveHardwareSlot() == target) {
        const std::uint8_t neighbour = static_cast<std::uint8_t>((target + 1) % SLOT_COUNT);
        (void)m_service.switchHardwareSlot(neighbour);
        if (!m_service.switchHardwareSlot(target)) {
            return {false, "The settings were applied, but returning to " + slotLabel(target) + " failed. Select the slot again to activate them."};
        }
    }

    return {true, "Actuation applied to " + slotLabel(target) + "."};
}

std::optional<ActuationSettings> ProfileServiceGateway::readActuation(const std::uint8_t slot,
                                                                      const std::optional<ActuationSettings>& expected) {
    if (m_keyboard.isDryRun() || !describeDevice().connected) return std::nullopt;

    const std::uint8_t target = toSlot(slot);

    // What the caller believes is stored there; falls back to the profile file, then to the defaults.
    ActuationSettings hint = expected.value_or(ActuationSettings{});
    if (!expected) {
        if (const auto profile = m_service.getProfileForSlot(target)) {
            hint = decodeStoredSettings(*profile).value_or(hint);
        }
    }
    kbd::GlobalTravelConfig hintConfig;
    hintConfig.mode              = hint.rapidTrigger ? kbd::GlobalTravelConfig::MODE_RAPID : kbd::GlobalTravelConfig::MODE_REGULAR;
    hintConfig.actuationTenths   = hint.actuationTenths;
    hintConfig.pressSensTenths   = hint.pressTenths;
    hintConfig.releaseSensTenths = hint.releaseTenths;
    const auto payload = kbd::ProfileMemory::encodeGlobalTravel(hintConfig);

    // A raw-profile "chunk" request for the first GLOBAL_TRAVEL_SIZE bytes. The firmware replies with the
    // stored bytes at [6..]; the payload we send is ignored by firmware that treats this opcode as a read.
    const auto request = KeychronProtocol::createMemoryChunkPacket(
        target, static_cast<std::uint16_t>(kbd::ProfileMemory::GLOBAL_TRAVEL_OFFSET), payload);

    m_keyboard.flushInputBuffer();
    if (!m_keyboard.sendReport(request)) return std::nullopt;

    const auto reply = awaitResponse(KeychronProtocol::AMC_GET_PROFILE_RAW, ACK_TIMEOUT);
    if (!reply || reply->size() < PROFILE_READ_DATA_OFFSET + kbd::ProfileMemory::GLOBAL_TRAVEL_SIZE) return std::nullopt;
    if ((*reply)[2] != target) return std::nullopt;

    const auto config = kbd::ProfileMemory::decodeGlobalTravel(
        std::span<const std::uint8_t>(reply->data() + PROFILE_READ_DATA_OFFSET, kbd::ProfileMemory::GLOBAL_TRAVEL_SIZE));
    if (!config) return std::nullopt;

    return actuation::fromGlobalTravel(*config);
}

CommandResult ProfileServiceGateway::saveSlot(const std::uint8_t slot) {
    const std::uint8_t target = toSlot(slot);
    CommandResult result = sendAcknowledged(KeychronProtocol::createSaveProfilePacket(target),
                                                  KeychronProtocol::AMC_SAVE_PROFILE, "save");
    if (!result.ok) return result;
    return {true, slotLabel(target) + " saved to the keyboard's flash."};
}

CommandResult ProfileServiceGateway::flashSlotProfile(const std::uint8_t slot) {
    const std::uint8_t target = toSlot(slot);

    const auto profile = m_service.getProfileForSlot(target);
    if (!profile) return {false, "No profile file is assigned to " + slotLabel(target) + "."};
    if (!describeDevice().connected) return {false, "The keyboard is not connected."};

    if (!m_service.applyProfile(*profile, true)) {
        return {false, "Writing '" + profile->m_name + "' to " + slotLabel(target) + " failed (see the log)."};
    }
    return {true, "'" + profile->m_name + "' was sent to " + slotLabel(target) + "."};
}

// ---------------------------------------------------------------------------
// Watcher events
// ---------------------------------------------------------------------------

void ProfileServiceGateway::handleFileCreated(const std::filesystem::path& path) {
    m_service.onFileCreated(path);
}

void ProfileServiceGateway::handleFileDeleted(const std::filesystem::path& path) {
    m_service.onFileDeleted(path);
}

void ProfileServiceGateway::handleDeviceConnected(const kbd::TransportMode mode, const std::string& devicePath) {
    m_service.onDeviceConnected(mode, devicePath);
}

void ProfileServiceGateway::handleDeviceDisconnected() {
    m_service.onDeviceDisconnected();
}

} // namespace ui
