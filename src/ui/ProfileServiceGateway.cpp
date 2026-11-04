#include "ui/ProfileServiceGateway.h"
#include "files/ProfileService.h"
#include "kbd/KeyboardDevice.h"
#include "kbd/KeychronProtocol.h"
#include "kbd/ProfileMemory.h"
#include "ui/KeyboardLayout.h"

#include <algorithm>
#include <cctype>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace ui {

namespace {

    using kbd::KeychronProtocol;

    constexpr std::chrono::milliseconds ACK_TIMEOUT{600};
    constexpr int                       READ_SLICE_MS = 100;

    // Reply layout of a raw-profile read (AMC_GET_PROFILE_RAW):
    // [0]=0xA9 [1]=0x12 [2]=slot [3..4]=offset [5]=size [6..]=data
    constexpr std::size_t PROFILE_READ_DATA_OFFSET = 6;

    // The parts of a profile beyond travel settings: SOCD pairs and keys with an advanced mode.
    ProfileExtras countExtras(std::span<const std::uint8_t> memory) {
        ProfileExtras extras;
        for (std::uint8_t row = 0; row < MATRIX_ROWS; ++row) {
            for (std::uint8_t col = 0; col < MATRIX_COLS; ++col) {
                if (!isKeyOnBoard({row, col})) continue;
                const auto key = kbd::ProfileMemory::decodeKeyTravel(memory, row, col);
                if (!key) continue;
                switch (key->advancedMode) {
                    case kbd::AdvancedMode::GAMEPAD: ++extras.gamepadKeys; break;
                    case kbd::AdvancedMode::TOGGLE:  ++extras.toggleKeys; break;
                    case kbd::AdvancedMode::DKS:     ++extras.dynamicKeys; break;
                    default: break;
                }
            }
        }
        for (std::size_t i = 0; i < kbd::ProfileMemory::SOCD_COUNT; ++i) {
            const auto pair = kbd::ProfileMemory::decodeSocd(memory, i);
            if (pair && pair->isSet()) ++extras.socdPairs;
        }
        return extras;
    }

    // What a profile file says: its global values, its per-key table and the extras.
    struct StoredTravel {
        std::optional<ActuationSettings> global;
        std::optional<KeyMatrix>         keys;
        ProfileExtras                    extras;
    };

    StoredTravel decodeStored(const KeyboardProfile& profile) {
        try {
            const auto memory = KeychronProtocol::extractMemoryBuffer(profile.m_configData);
            StoredTravel stored;
            if (const auto config = kbd::ProfileMemory::decodeGlobalTravel(memory)) {
                stored.global = actuation::fromGlobalTravel(*config);
            }
            stored.keys   = actuation::keyMatrixFromProfile(memory);
            stored.extras = countExtras(memory);
            return stored;
        } catch (const std::exception&) {
            return {}; // the file has no (valid) settings: show the profile without actuation details
        }
    }

    // Matrix position -> bit in the AMC_SET_TRAVEL row masks.
    std::array<std::uint32_t, KeychronProtocol::MATRIX_ROWS> rowMasksFor(const std::vector<KeyPosition>& keys) {
        std::array<std::uint32_t, KeychronProtocol::MATRIX_ROWS> masks{};
        for (const KeyPosition& key : keys) {
            if (key.row >= KeychronProtocol::MATRIX_ROWS || key.col >= KeychronProtocol::MATRIX_COLS) continue;
            masks[key.row] |= 1u << key.col;
        }
        return masks;
    }

    // Keys of a matrix grouped by identical settings, so each group is one AMC_SET_TRAVEL.
    std::vector<std::pair<KeyActuation, std::vector<KeyPosition>>> groupByValue(const KeyMatrix& matrix) {
        std::vector<std::pair<KeyActuation, std::vector<KeyPosition>>> groups;
        for (std::uint8_t row = 0; row < MATRIX_ROWS; ++row) {
            for (std::uint8_t col = 0; col < MATRIX_COLS; ++col) {
                const KeyPosition key{row, col};
                const KeyActuation value = actuation::sanitized(matrix.at(key));
                auto it = std::find_if(groups.begin(), groups.end(), [&](const auto& group) { return group.first == value; });
                if (it == groups.end()) {
                    groups.push_back({value, {key}});
                } else {
                    it->second.push_back(key);
                }
            }
        }
        return groups;
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
        info.variant   = kbd::KeyboardVariant::Iso;
        return info;
    }

    info.connected = m_keyboard.isConnected();
    if (!info.connected) return info;

    info.path      = m_keyboard.getDevicePath();
    info.transport = kbd::TransportMode::Wired; // the only transport the app talks over
    info.variant   = m_keyboard.variant();
    return info;
}

std::array<SlotInfo, SLOT_COUNT> ProfileServiceGateway::describeSlots() const {
    std::array<SlotInfo, SLOT_COUNT> slots;

    for (std::size_t i = 0; i < SLOT_COUNT; ++i) {
        SlotInfo& info = slots[i];
        info.name = slotLabel(i);

        const auto profile = m_service.getProfileForSlot(static_cast<std::uint8_t>(i));
        if (!profile) continue;

        const StoredTravel stored = decodeStored(*profile);
        info.hasProfile     = true;
        info.name           = profile->m_name;
        info.fileName       = profile->m_filePath.filename().string();
        info.storedSettings = stored.global;
        info.storedKeys     = stored.keys;
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

    for (auto now = std::chrono::steady_clock::now(); now < deadline; now = std::chrono::steady_clock::now()) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        const int bytes = m_keyboard.readReport(report, static_cast<int>(std::clamp<long long>(left, 1, READ_SLICE_MS)));
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

    if (!reloadIfActive(target)) {
        return {false, "The settings were applied, but returning to " + slotLabel(target) + " failed. Select the slot again to activate them."};
    }
    return {true, "Actuation applied to " + slotLabel(target) + "."};
}

// The firmware writes a "whole keyboard" setting into the slot's RAM profile but only rebuilds the per-key
// tables it actually scans with when the ACTIVE profile changes (analog_matrix/profile.c: profile_set_traval()
// -> no update_travel_configs() on the global path). If we just edited the active slot, hop to a neighbouring
// slot and back so the new values take effect immediately.
bool ProfileServiceGateway::reloadIfActive(const std::uint8_t target) {
    if (m_service.queryActiveHardwareSlot() != target) return true;
    const std::uint8_t neighbour = static_cast<std::uint8_t>((target + 1) % SLOT_COUNT);
    (void)m_service.switchHardwareSlot(neighbour);
    return m_service.switchHardwareSlot(target);
}

CommandResult ProfileServiceGateway::applyKeyActuation(const std::uint8_t slot, const std::vector<KeyPosition>& keys,
                                                       const KeyActuation& requested) {
    const std::uint8_t target = toSlot(slot);
    const auto masks = rowMasksFor(keys);
    if (std::all_of(masks.begin(), masks.end(), [](const std::uint32_t mask) { return mask == 0; })) {
        return {false, "No keys selected."};
    }

    // Per-key values take effect at once on the active slot (profile_set_traval -> update_key_config), so no hop.
    const KeyActuation value = actuation::sanitized(requested);
    const CommandResult sent = sendAcknowledged(
        KeychronProtocol::createKeyTravelPacket(target, value.mode, value.actuationTenths, value.pressTenths,
                                                value.releaseTenths, masks),
        KeychronProtocol::AMC_SET_TRAVEL, "per-key actuation");
    if (!sent.ok) return sent;

    const std::string count = std::to_string(keys.size()) + (keys.size() == 1 ? " key" : " keys");
    if (value.followsGlobal()) return {true, count + " of " + slotLabel(target) + " now follow the slot settings."};
    return {true, count + " of " + slotLabel(target) + " updated."};
}

std::optional<std::vector<std::uint8_t>> ProfileServiceGateway::readProfileRange(const std::uint8_t slot, const std::size_t from,
                                                                                 const std::size_t to) {
    if (m_keyboard.isDryRun() || !describeDevice().connected) return std::nullopt;

    const std::uint8_t target = toSlot(slot);
    std::vector<std::uint8_t> memory(kbd::ProfileMemory::PROFILE_SIZE, 0);
    const std::size_t end = std::min(to, memory.size());

    for (std::size_t offset = from; offset < end;) {
        const auto size = static_cast<std::uint8_t>(std::min(KeychronProtocol::MAX_CHUNK_PAYLOAD, end - offset));

        m_keyboard.flushInputBuffer();
        if (!m_keyboard.sendReport(KeychronProtocol::createProfileReadPacket(target, static_cast<std::uint16_t>(offset), size))) {
            return std::nullopt;
        }
        const auto reply = awaitResponse(KeychronProtocol::AMC_GET_PROFILE_RAW, ACK_TIMEOUT);
        if (!reply || reply->size() < PROFILE_READ_DATA_OFFSET + size) return std::nullopt;

        // The reply echoes slot and offset; anything else is a stale answer to an earlier request.
        const auto replyOffset = static_cast<std::size_t>((*reply)[3] | ((*reply)[4] << 8));
        if ((*reply)[2] != target || replyOffset != offset) return std::nullopt;

        std::copy_n(reply->begin() + static_cast<std::ptrdiff_t>(PROFILE_READ_DATA_OFFSET), size,
                    memory.begin() + static_cast<std::ptrdiff_t>(offset));
        offset += size;
    }
    return memory;
}

std::optional<KeyMatrix> ProfileServiceGateway::readKeyMatrix(const std::uint8_t slot) {
    // 384 bytes of key_config, 26 bytes per report (15 round-trips).
    const auto memory = readProfileRange(slot, kbd::ProfileMemory::KEY_CONFIG_OFFSET,
                                         kbd::ProfileMemory::KEY_CONFIG_OFFSET + kbd::ProfileMemory::KEY_CONFIG_SIZE);
    if (!memory) return std::nullopt;
    return actuation::keyMatrixFromProfile(*memory);
}

std::optional<ActuationSettings> ProfileServiceGateway::readActuation(const std::uint8_t slot,
                                                                      const std::optional<ActuationSettings>& expected) {
    if (m_keyboard.isDryRun() || !describeDevice().connected) return std::nullopt;

    const std::uint8_t target = toSlot(slot);

    // What the caller believes is stored there; falls back to the profile file, then to the defaults.
    ActuationSettings hint = expected.value_or(ActuationSettings{});
    if (!expected) {
        if (const auto profile = m_service.getProfileForSlot(target)) {
            hint = decodeStored(*profile).global.value_or(hint);
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

    return writeProfile(target, *profile);
}

// The firmware has no "write raw profile" command (0x12 only reads), so a profile is written the way the
// Launcher does it: the global travel settings, one AMC_SET_TRAVEL per group of keys that share values, every
// key's advanced mode, the 20 SOCD entries, the name, then AMC_SAVE_PROFILE to commit everything to flash.
CommandResult ProfileServiceGateway::writeProfile(const std::uint8_t target, const KeyboardProfile& profile) {
    std::array<std::uint8_t, KeychronProtocol::PROFILE_MEMORY_SIZE> memory{};
    try {
        memory = KeychronProtocol::extractMemoryBuffer(profile.m_configData);
    } catch (const std::exception& e) {
        return {false, "'" + profile.m_name + "' has no settings the keyboard understands (" + e.what() + ")."};
    }

    const auto globalConfig = kbd::ProfileMemory::decodeGlobalTravel(memory);
    const auto keys         = actuation::keyMatrixFromProfile(memory);
    if (!globalConfig || !keys) return {false, "'" + profile.m_name + "' could not be decoded."};
    const ProfileExtras extras = countExtras(memory);

    // 1. Global settings (firmware rules for 0 / out-of-range values applied, as when it loads a profile).
    const ActuationSettings global = actuation::fromGlobalTravel(*globalConfig);
    CommandResult step = sendAcknowledged(
        KeychronProtocol::createFastTravelPacket(target, global.rapidTrigger, global.actuationTenths,
                                                 global.pressTenths, global.releaseTenths, true),
        KeychronProtocol::AMC_SET_TRAVEL, "global actuation");
    if (!step.ok) return step;

    // 2. Every key, including the ones that follow the slot (that clears overrides left from earlier).
    for (const auto& [value, group] : groupByValue(*keys)) {
        step = sendAcknowledged(
            KeychronProtocol::createKeyTravelPacket(target, value.mode, value.actuationTenths, value.pressTenths,
                                                    value.releaseTenths, rowMasksFor(group)),
            KeychronProtocol::AMC_SET_TRAVEL, "per-key actuation");
        if (!step.ok) return step;
    }

    // 3. Advanced mode of every key on the board: game-controller targets, toggle keys and dynamic keystrokes are
    //    set, everything else is cleared, so nothing survives from the profile the slot held before (the Launcher
    //    does the same).
    std::size_t dynamicWritten = 0;
    std::size_t dynamicSkipped = 0;
    for (std::uint8_t row = 0; row < MATRIX_ROWS; ++row) {
        for (std::uint8_t col = 0; col < MATRIX_COLS; ++col) {
            if (!isKeyOnBoard({row, col})) continue;
            const auto key = kbd::ProfileMemory::decodeKeyTravel(memory, row, col);
            std::uint8_t advMode = KeychronProtocol::ADV_MODE_CLEAR;
            std::uint8_t index   = 0;
            if (key && key->advancedMode == kbd::AdvancedMode::GAMEPAD) {
                advMode = KeychronProtocol::ADV_MODE_GAME_CONTROLLER;
                index   = key->advancedData;
            } else if (key && key->advancedMode == kbd::AdvancedMode::TOGGLE) {
                advMode = KeychronProtocol::ADV_MODE_TOGGLE;
            } else if (key && key->advancedMode == kbd::AdvancedMode::DKS) {
                // Dynamic keystroke: its OKMC entry travels with the key in the same command.
                const auto okmc = kbd::ProfileMemory::decodeOkmc(memory, key->advancedData);
                if (okmc && !okmc->isEmpty()) {
                    step = sendAcknowledged(KeychronProtocol::createOkmcPacket(target, row, col, key->advancedData, *okmc),
                                            KeychronProtocol::AMC_SET_ADVANCE_MODE, "dynamic keystroke");
                    if (!step.ok) return step;
                    ++dynamicWritten;
                    continue;
                }
                ++dynamicSkipped; // the file names a DKS entry but carries no keycodes for it
            }
            step = sendAcknowledged(KeychronProtocol::createAdvanceModePacket(target, advMode, row, col, index),
                                    KeychronProtocol::AMC_SET_ADVANCE_MODE, "advanced key mode");
            if (!step.ok) return step;
        }
    }

    // 4. All SOCD entries: the file's pairs, the rest cleared.
    for (std::size_t i = 0; i < kbd::ProfileMemory::SOCD_COUNT; ++i) {
        const kbd::SocdPair pair = kbd::ProfileMemory::decodeSocd(memory, i).value_or(kbd::SocdPair{});
        const bool valid = pair.isSet() && pair.type <= kbd::SocdPair::TYPE_MAX && pair.row1 < MATRIX_ROWS &&
                           pair.col1 < MATRIX_COLS && pair.row2 < MATRIX_ROWS && pair.col2 < MATRIX_COLS &&
                           !(pair.row1 == pair.row2 && pair.col1 == pair.col2);
        step = valid ? sendAcknowledged(KeychronProtocol::createSocdPacket(target, static_cast<std::uint8_t>(i), pair.row1, pair.col1,
                                                                           pair.row2, pair.col2, pair.type),
                                        KeychronProtocol::AMC_SET_SOCD, "SOCD")
                     : sendAcknowledged(KeychronProtocol::createSocdPacket(target, static_cast<std::uint8_t>(i), 0, 0, 0, 0, 0),
                                        KeychronProtocol::AMC_SET_SOCD, "SOCD");
        if (!step.ok) return step;
    }

    // 5. Name (shown by the Launcher); a failure here is reported but does not undo the rest.
    std::string notes;
    if (!profile.m_name.empty()) {
        const CommandResult named = sendAcknowledged(KeychronProtocol::createSetProfileNamePacket(target, profile.m_name),
                                                     KeychronProtocol::AMC_SET_PROFILE_NAME, "profile name");
        if (!named.ok) notes += " The name was not accepted.";
    }

    // 6. Commit.
    step = sendAcknowledged(KeychronProtocol::createSaveProfilePacket(target), KeychronProtocol::AMC_SAVE_PROFILE, "save");
    if (!step.ok) return {false, "The settings were sent but saving them to flash failed: " + step.message};

    if (!reloadIfActive(target)) notes += " Select " + slotLabel(target) + " again to activate the new global values.";
    if (dynamicSkipped > 0) notes += " " + std::to_string(dynamicSkipped) + " dynamic-keystroke keys had no keycodes in the file and act as normal keys.";

    std::string summary = actuation::describe(global);
    const std::size_t overrides = keys->overrideCount();
    if (overrides > 0) summary += ", " + std::to_string(overrides) + " keys with their own settings";
    if (extras.socdPairs > 0) summary += ", " + std::to_string(extras.socdPairs) + " SOCD pairs";
    if (extras.gamepadKeys > 0) summary += ", " + std::to_string(extras.gamepadKeys) + " gamepad keys";
    if (extras.toggleKeys > 0) summary += ", " + std::to_string(extras.toggleKeys) + " toggle keys";
    if (dynamicWritten > 0) summary += ", " + std::to_string(dynamicWritten) + " dynamic keystrokes";
    return {true, "'" + profile.m_name + "' written to " + slotLabel(target) + ": " + summary + "." + notes};
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


// ---------------------------------------------------------------------------
// Profile library
// ---------------------------------------------------------------------------

std::vector<LibraryProfileInfo> ProfileServiceGateway::describeLibrary() const {
    std::vector<LibraryProfileInfo> library;

    m_service.forEachProfile([&](const KeyboardProfile& profile) {
        const StoredTravel stored = decodeStored(profile);
        LibraryProfileInfo info;
        info.name         = profile.m_name;
        info.fileName     = profile.m_filePath.filename().string();
        info.assignedSlot = kbd::ProfileService::slotOf(profile);
        info.actuation    = stored.global;
        info.keyOverrides = stored.keys ? stored.keys->overrideCount() : 0;
        info.extras       = stored.extras;
        library.push_back(std::move(info));
    });
    return library;
}

CommandResult ProfileServiceGateway::reorderLibrary(const std::size_t fromIndex, const std::size_t toIndex) {
    if (!m_service.reorderProfiles(fromIndex, toIndex)) return {false, "That profile cannot be moved there."};
    return {true, "Profile order updated."};
}

CommandResult ProfileServiceGateway::assignProfileToSlot(const std::string& fileName, const std::uint8_t slot, const bool writeNow) {
    const std::uint8_t target = toSlot(slot);
    const auto profile = m_service.findProfileByFileName(fileName);
    if (!profile) return {false, "Profile file not found: " + fileName};

    if (!m_service.assignSlot(fileName, target)) {
        return {false, "Could not save the slot assignment in " + fileName + "."};
    }
    if (!writeNow) return {true, "'" + profile->m_name + "' is now the profile of " + slotLabel(target) + "."};

    if (!describeDevice().connected) {
        return {false, "'" + profile->m_name + "' was assigned to " + slotLabel(target) + ", but the keyboard is not connected to write it."};
    }
    const auto assigned = m_service.getProfileForSlot(target);
    return writeProfile(target, assigned ? *assigned : *profile);
}

CommandResult ProfileServiceGateway::clearSlot(const std::uint8_t slot) {
    const std::uint8_t target = toSlot(slot);
    if (!m_service.unassignSlot(target)) return {false, "No profile file is assigned to " + slotLabel(target) + "."};
    return {true, slotLabel(target) + " has no profile file now; the file stays in the library."};
}

// ---------------------------------------------------------------------------
// Advanced keys, SOCD, game controller
// ---------------------------------------------------------------------------

std::optional<SlotDetails> ProfileServiceGateway::readSlotDetails(const std::uint8_t slot) {
    // key_config, okmc and socd: 824 bytes in 32 round-trips.
    const auto memory = readProfileRange(slot, kbd::ProfileMemory::KEY_CONFIG_OFFSET, kbd::ProfileMemory::NAME_OFFSET);
    if (!memory) return std::nullopt;
    return SlotDetails::fromMemory(*memory);
}

CommandResult ProfileServiceGateway::applyAdvancedKey(const std::uint8_t slot, const AdvancedKeyChange& change) {
    const std::uint8_t target = toSlot(slot);
    const KeyPosition& key = change.key;
    if (key.row >= MATRIX_ROWS || key.col >= MATRIX_COLS) return {false, "That key is outside the keyboard matrix."};

    switch (change.kind) {
        case AdvancedKeyChange::Kind::Gamepad:
            if (!kbd::GamepadTarget::isValid(change.index)) return {false, "Unknown game-controller target."};
            return sendAcknowledged(KeychronProtocol::createAdvanceModePacket(target, KeychronProtocol::ADV_MODE_GAME_CONTROLLER,
                                                                              key.row, key.col, change.index),
                                    KeychronProtocol::AMC_SET_ADVANCE_MODE, "gamepad key");
        case AdvancedKeyChange::Kind::Toggle:
            return sendAcknowledged(KeychronProtocol::createAdvanceModePacket(target, KeychronProtocol::ADV_MODE_TOGGLE, key.row, key.col, 0),
                                    KeychronProtocol::AMC_SET_ADVANCE_MODE, "toggle key");
        case AdvancedKeyChange::Kind::DynamicKeystroke:
            if (change.index >= kbd::ProfileMemory::OKMC_COUNT) return {false, "All 20 dynamic-keystroke entries of the slot are in use."};
            if (change.okmc.isEmpty()) return {false, "A dynamic keystroke needs at least one keycode."};
            return sendAcknowledged(KeychronProtocol::createOkmcPacket(target, key.row, key.col, change.index, change.okmc),
                                    KeychronProtocol::AMC_SET_ADVANCE_MODE, "dynamic keystroke");
        case AdvancedKeyChange::Kind::Clear:
            break;
    }
    return sendAcknowledged(KeychronProtocol::createAdvanceModePacket(target, KeychronProtocol::ADV_MODE_CLEAR, key.row, key.col, 0),
                            KeychronProtocol::AMC_SET_ADVANCE_MODE, "plain key");
}

CommandResult ProfileServiceGateway::applySocd(const std::uint8_t slot, const std::size_t index, const kbd::SocdPair& pair) {
    if (index >= kbd::ProfileMemory::SOCD_COUNT) return {false, "SOCD entry out of range."};
    const kbd::SocdPair entry = pair.isSet() ? pair : kbd::SocdPair{};
    return sendAcknowledged(KeychronProtocol::createSocdPacket(toSlot(slot), static_cast<std::uint8_t>(index), entry.row1, entry.col1,
                                                               entry.row2, entry.col2, entry.type),
                            KeychronProtocol::AMC_SET_SOCD, "SOCD");
}

std::optional<ControllerSettings> ProfileServiceGateway::readControllerSettings() {
    if (m_keyboard.isDryRun() || !describeDevice().connected) return std::nullopt;

    ControllerSettings settings;
    m_keyboard.flushInputBuffer();
    if (m_keyboard.sendReport(KeychronProtocol::createControllerModeReadPacket())) {
        if (const auto reply = awaitResponse(KeychronProtocol::AMC_GET_GAME_CONTROLLER_MODE, ACK_TIMEOUT)) {
            settings.mode = KeychronProtocol::parseControllerMode(*reply);
        }
    }
    m_keyboard.flushInputBuffer();
    if (m_keyboard.sendReport(KeychronProtocol::createCurveReadPacket())) {
        if (const auto reply = awaitResponse(KeychronProtocol::AMC_GET_CURVE, ACK_TIMEOUT)) {
            settings.curve = KeychronProtocol::parseCurve(*reply);
        }
    }
    if (!settings.mode && !settings.curve) return std::nullopt;
    return settings;
}

CommandResult ProfileServiceGateway::applyControllerMode(const std::uint8_t mode) {
    if (!describeDevice().connected) return {false, "The keyboard is not connected."};
    m_keyboard.flushInputBuffer();
    if (!m_keyboard.sendReport(KeychronProtocol::createControllerModePacket(mode))) return {false, "Could not send the controller mode."};
    if (m_keyboard.isDryRun()) return {true, "Controller mode sent (dry run)."};
    // game_controller_mode_set() leaves no status byte in the reply (it echoes the mode), so any answer is the ack.
    if (!awaitResponse(KeychronProtocol::AMC_SET_GAME_CONTROLLER_MODE, ACK_TIMEOUT)) {
        return {false, "The keyboard did not acknowledge the controller mode."};
    }
    return {true, "Controller mode accepted."};
}

CommandResult ProfileServiceGateway::applyCurve(const kbd::ResponseCurve& curve) {
    if (!kbd::isValidCurve(curve)) return {false, "The curve's points are out of order or out of range."};
    return sendAcknowledged(KeychronProtocol::createCurvePacket(curve), KeychronProtocol::AMC_SET_CURVE, "response curve");
}

std::optional<LiveKeyTravel> ProfileServiceGateway::readLiveTravel(const KeyPosition& key) {
    if (m_keyboard.isDryRun() || !m_keyboard.isConnected()) return std::nullopt;
    m_keyboard.flushInputBuffer();
    if (!m_keyboard.sendReport(KeychronProtocol::createRealtimeTravelPacket(key.row, key.col))) return std::nullopt;
    const auto reply = awaitResponse(KeychronProtocol::AMC_GET_REALTIME_TRAVEL, std::chrono::milliseconds(40));
    if (!reply) return std::nullopt;
    const auto travel = KeychronProtocol::parseRealtimeTravel(*reply, key.row, key.col);
    if (!travel) return std::nullopt;
    return LiveKeyTravel{key, travel->travelTenths, travel->travelRaw, travel->pressed};
}

std::optional<bool> ProfileServiceGateway::enumeratedXInput() {
    return m_keyboard.enumeratedAsXInput();
}

} // namespace ui
