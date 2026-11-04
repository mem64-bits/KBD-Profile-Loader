#include "files/ProfileService.h"
#include "kbd/KeyboardDevice.h"
#include "kbd/KeychronProtocol.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <optional>
#include <string_view>
#include <utility>

using namespace std::chrono_literals;

namespace kbd {

ProfileService::ProfileService(KeyboardDevice& keyboard)
    : m_keyboard(keyboard)
{}

void ProfileService::loadProfileFromFile(const fs::path& filePath) {
    if (!fs::exists(filePath) || filePath.extension() != ".json") return;

    try {
        std::ifstream file(filePath);
        nlohmann::json data = nlohmann::json::parse(file);
        std::string name = data.value("name", displayNameFor(filePath));

        KeyboardProfile profile(name, filePath, std::move(data));

        if (m_profileList.contains(profile)) {
            m_profileList.remove(profile);
        }
        m_profileList.addLast(profile);
        std::cout << "[ProfileService] Loaded profile: " << name << '\n';
    } catch (const std::exception& e) {
        std::cerr << "[ProfileService] Error reading " << filePath.filename().string()
                << ": " << e.what() << '\n';
    }
}

void ProfileService::loadAllFromDirectory(const fs::path& folder) {
    if (!fs::exists(folder)) return;

    std::vector<KeyboardProfile> tempProfiles;

    for (const auto& entry : fs::directory_iterator(folder)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            try {
                std::ifstream file(entry.path());
                nlohmann::json data = nlohmann::json::parse(file);
                std::string name = data.value("name", displayNameFor(entry.path()));
                tempProfiles.emplace_back(name, entry.path(), std::move(data));
            } catch (...) {}
        }
    }

    // 1. Profiles assigned to a slot first (Slot 1 -> Slot 2 -> Slot 3), then the library-only ones by file name
    std::ranges::sort(tempProfiles, [](const KeyboardProfile& a, const KeyboardProfile& b) {
        const int slotA = slotOf(a) ? *slotOf(a) : 255;
        const int slotB = slotOf(b) ? *slotOf(b) : 255;
        if (slotA != slotB) return slotA < slotB;
        return a.m_filePath.filename() < b.m_filePath.filename();
    });

    // 2. Add them in sorted order to the doubly linked list
    for (auto& profile : tempProfiles) {
        if (m_profileList.contains(profile)) {
            m_profileList.remove(profile);
        }
        const auto slot = slotOf(profile);
        std::cout << "[ProfileService] Loaded profile (" << (slot ? "Slot " + std::to_string(*slot + 1) : std::string("library only"))
                << "): " << profile.m_name << '\n';
        m_profileList.addLast(profile);
    }
}

void ProfileService::onFileCreated(const fs::path& path) {
    loadProfileFromFile(path);
}

void ProfileService::onFileDeleted(const fs::path& path) {
    // Profiles are identified by their file name; look the loaded one up first so the log shows its name.
    KeyboardProfile target(path.stem().string(), path, {});
    m_profileList.forEach([&](const KeyboardProfile& p) {
        if (p.m_filePath.filename() == path.filename()) {
            target = p;
        }
    });

    m_profileList.remove(target);
    std::cout << "[ProfileService] Removed profile: " << target.m_name << '\n';
}

void ProfileService::onDeviceConnected(TransportMode /*mode*/, const std::string& /*path*/) {
    std::cout << "\n[Hardware Auto-Sync] Reconnected over USB-C. Re-applying active profile...\nkbd> " << std::flush;

    if (m_profileList.size() > 0) {
        applyProfile(m_profileList.getCurrent(), false);
    }
}

void ProfileService::onDeviceDisconnected() {
    std::cout << "\n[Hardware Notice] Keyboard disconnected (USB cable unplugged).\nkbd> " << std::flush;
}

void ProfileService::cycleForward() {
    cycle(true);
}

void ProfileService::cycleBackward() {
    cycle(false);
}

// Steps through the list in its (user-defined) order, skipping library-only profiles: only a profile that
// is assigned to one of the keyboard's slots can be switched to.
bool ProfileService::cycle(const bool forward) {
    if (m_profileList.size() == 0) {
        std::cout << "[ProfileService] No profiles loaded.\n";
        return false;
    }
    for (std::size_t step = 0; step < m_profileList.size(); ++step) {
        const KeyboardProfile& profile = forward ? m_profileList.cycleNext() : m_profileList.cyclePrev();
        if (slotOf(profile)) return applyProfile(profile, false);
    }
    std::cout << "[ProfileService] No profile is assigned to a slot.\n";
    return false;
}

bool ProfileService::applyProfile(const KeyboardProfile& profile, bool flashFullMemory) {
    std::cout << "\n========================================\n";
    std::cout << ">>> ACTIVATING: " << profile.m_name << '\n';
    std::cout << "========================================\n";

    if (!m_keyboard.isConnected() && !m_keyboard.isDryRun()) {
        std::cout << "[Warning] Keyboard disconnected. Reconnecting...\n";
        if (!m_keyboard.connect()) {
            std::cerr << "[Error] Cannot apply profile: Keyboard unavailable.\n";
            return false;
        }
    }

    const auto assigned = slotOf(profile);
    if (!assigned) {
        std::cerr << "[Error] '" << profile.m_name << "' is not assigned to a slot.\n";
        return false;
    }
    const uint8_t targetSlot = *assigned;

    if (flashFullMemory) {
        if (!flashProfileMemory(targetSlot, profile)) return false;
    }

    // Direct clean switch (no 0x13 name packet polluting the pipe)
    bool ok = switchHardwareSlot(targetSlot);

    // Sync cursor to this profile
    m_profileList.setCursorTo(profile);
    return ok;
}

bool ProfileService::switchHardwareSlot(uint8_t slotIndex) {
    uint8_t slot = slotIndex % 3;

    m_keyboard.flushInputBuffer();

    auto packet = KeychronProtocol::createSlotSelectPacket(slot);
    bool ok = m_keyboard.sendReport(packet);
    if (!ok) {
        std::cerr << "[Hardware Sync] Failed to send slot select packet to USB pipe.\n";
        return false;
    }

    // 1. Drain the 0x11 return code packet (where Byte 2 is 0x00 SUCCESS)
    std::vector<uint8_t> selectAck;
    m_keyboard.readReport(selectAck, 40);

    // 2. Settling delay for QMK MCU to reload internal curve tables
    std::this_thread::sleep_for(30ms);

    // 3. Query AMC_GET_PROFILES_INFO (0x10) to read back the true active slot register
    int confirmedSlot = queryActiveHardwareSlot();

    if (confirmedSlot != -1) {
        std::cout << "[Hardware ACK] Confirmed by Keyboard MCU -> Active Slot: "
                << (confirmedSlot + 1) << " [" << getProfileNameForSlot(confirmedSlot) << "] "
                << (confirmedSlot == slot ? "[MATCH]" : "[MISMATCH!]") << "\n";

        // Sync linked list cursor to match the confirmed active hardware slot
        std::optional<KeyboardProfile> owner;
        std::as_const(m_profileList).forEach([&](const KeyboardProfile& p) {
            if (!owner && slotOf(p) == static_cast<uint8_t>(confirmedSlot)) owner = p;
        });
        if (owner) m_profileList.setCursorTo(*owner);
    } else {
        std::cout << "[Hardware Sync] Sent 0xA9 0x11 -> Slot " << static_cast<int>( slot ) + 1 << '\n';
    }

    return (confirmedSlot == slot);
}

bool ProfileService::flashProfileMemory(uint8_t slotIndex, const KeyboardProfile& profile) const
{
    try {
        std::cout << "[Flash Engine] Extracting 860-byte payload from: " << profile.m_name << '\n';
        auto memBuffer = KeychronProtocol::extractMemoryBuffer(profile.m_configData);
        auto packets = KeychronProtocol::generateMemoryStreamPackets(slotIndex, memBuffer);

        std::cout << "[Flash Engine] Streaming " << packets.size()
                << " chunks to Slot " << static_cast<int>( slotIndex ) + 1 << " (AMC_GET_PROFILE_RAW: 0x12)...\n";

        for (size_t i = 0; i < packets.size(); ++i) {
            if (!m_keyboard.sendReport(packets[i])) {
                std::cerr << "[Flash Engine] Write failed on chunk index " << i << '\n';
                return false;
            }
            std::this_thread::sleep_for(5ms);
        }

        // QMK Hardware Commit: Send AMC_SAVE_PROFILE (0x1F) to burn RAM into Flash/EEPROM
        std::cout << "[Flash Engine] Sending AMC_SAVE_PROFILE (0x1F) to commit to SPI Flash...\n";
        auto savePacket = KeychronProtocol::createSaveProfilePacket(slotIndex);
        m_keyboard.sendReport(savePacket);

        // Wait for MCU to commit SPI Flash and drain the 0x1F ACK packet
        std::vector<uint8_t> saveAck;
        m_keyboard.readReport(saveAck, 100);

        std::cout << "[Flash Engine] Successfully flashed & saved Slot " << static_cast<int>(slotIndex) + 1 << " permanently.\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Flash Engine] Flashing aborted: " << e.what() << '\n';
        return false;
    }
}

bool ProfileService::applyFastTravel(
    uint8_t slotIndex,
    bool rapidTrigger,
    uint8_t actPointTenths,
    uint8_t pressSensTenths,
    uint8_t releaseSensTenths
) const
{
    auto packet = KeychronProtocol::createFastTravelPacket(
        slotIndex, rapidTrigger, actPointTenths, pressSensTenths, releaseSensTenths, true
    );
    bool ok = m_keyboard.sendReport(packet);
    if (ok) {
        std::cout << "[Fast Travel] Applied AMC_SET_TRAVAL (0x14) -> "
                << "Actuation: " << static_cast<float>( actPointTenths ) / 10.0f << "mm, "
                << "RT: " << (rapidTrigger ? "ON" : "OFF") << " ("
                << static_cast<float>( pressSensTenths ) / 10.0f << "mm / "
                << static_cast<float>( releaseSensTenths ) / 10.0f << "mm)\n";
    }
    return ok;
}

bool ProfileService::syncProfileNameToHardware(uint8_t slotIndex, const std::string& name) const
{
    const auto packet = KeychronProtocol::createSetProfileNamePacket(slotIndex, name);
    return m_keyboard.sendReport(packet);
}

bool ProfileService::writeProfileFile(const KeyboardProfile& profile) {
    std::ofstream file(profile.m_filePath, std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "[ProfileService] Cannot write " << profile.m_filePath.string() << '\n';
        return false;
    }
    // `replace`: a file with invalid UTF-8 in some string is still written instead of throwing.
    file << profile.m_configData.dump(4, ' ', false, nlohmann::json::error_handler_t::replace);
    file.flush();
    return static_cast<bool>(file);
}

bool ProfileService::assignSlot(const std::string& fileName, const uint8_t slotIndex) {
    const uint8_t targetSlot = slotIndex % 3;

    bool found = false;
    bool ok    = true;
    m_profileList.forEach([&](KeyboardProfile& p) {
        const bool isTarget = p.m_filePath.filename().string() == fileName;
        if (isTarget) {
            found = true;
            p.m_configData.erase("slot"); // legacy key; "current" is the one the Launcher writes
            p.m_configData["current"] = targetSlot;
            ok = writeProfileFile(p) && ok;
        } else if (slotOf(p) == targetSlot) {
            // A slot holds one profile: the file that had it goes back to the library.
            p.m_configData.erase("current");
            p.m_configData.erase("slot");
            ok = writeProfileFile(p) && ok;
        }
    });

    if (!found) return false;
    std::cout << "[ProfileService] " << fileName << " -> Slot " << static_cast<int>(targetSlot) + 1 << '\n';
    return ok;
}

int ProfileService::queryActiveHardwareSlot() {
    if (!m_keyboard.isConnected() && !m_keyboard.isDryRun()) {
        if (!m_keyboard.connect()) return -1;
    }

    m_keyboard.flushInputBuffer();

    auto queryPacket = KeychronProtocol::createStatusQueryPacket();
    if (!m_keyboard.sendReport(queryPacket)) {
        return -1;
    }

    // Other programs talking to the same raw-HID interface (Keychron Launcher, OpenRGB's Keychron support)
    // get their replies copied to our handle as well, so only an AMC_GET_PROFILES_INFO answer counts.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
    std::vector<uint8_t> response;
    while (std::chrono::steady_clock::now() < deadline) {
        const int bytes = m_keyboard.readReport(response, 50);
        if (bytes < 0) return -1;
        if (bytes >= 3 && response[0] == KeychronProtocol::CMD_ANALOG_MATRIX &&
            response[1] == KeychronProtocol::AMC_GET_PROFILES_INFO) {
            return static_cast<int>(response[2] % 3);
        }
        if (m_keyboard.isDryRun()) break;
    }
    return -1;
}

    std::string ProfileService::getProfileNameForSlot(uint8_t slotIndex) const {
        uint8_t target = slotIndex % 3;
        std::string matchedName;

        m_profileList.forEach([&](const KeyboardProfile& p) {
            if (slotOf(p) == target && matchedName.empty()) {
                matchedName = p.m_name;
            }
        });

        if (!matchedName.empty()) return matchedName;

        switch (target) {
            case 0: return "Slot 1 (Default)";
            case 1: return "Slot 2 (Gaming)";
            case 2: return "Slot 3 (Gamepad)";
            default: return "Slot " + std::to_string(target + 1);
        }
    }

std::optional<KeyboardProfile> ProfileService::getProfileForSlot(uint8_t slotIndex) const {
    const uint8_t target = slotIndex % 3;
    std::optional<KeyboardProfile> match;

    m_profileList.forEach([&](const KeyboardProfile& p) {
        if (!match && slotOf(p) == target) {
            match = p;
        }
    });
    return match;
}

std::size_t ProfileService::profileCount() const noexcept {
    return m_profileList.size();
}

const KeyboardProfile& ProfileService::getCurrentProfile() const {
    return m_profileList.getCurrent();
}

std::string ProfileService::displayNameFor(const fs::path& filePath) {
    std::string name = filePath.stem().string();

    constexpr std::string_view LAUNCHER_PREFIX = "HEProfile-";
    if (name.size() > LAUNCHER_PREFIX.size() && name.compare(0, LAUNCHER_PREFIX.size(), LAUNCHER_PREFIX) == 0) {
        name.erase(0, LAUNCHER_PREFIX.size());
    }

    // Trailing "-yyyy-mm-dd" (the export date)
    const auto isDigit = [](const char c) { return c >= '0' && c <= '9'; };
    constexpr std::size_t DATE_SUFFIX = 11; // "-2026-10-28"
    if (name.size() > DATE_SUFFIX) {
        const std::string_view tail(name.data() + name.size() - DATE_SUFFIX, DATE_SUFFIX);
        const bool isDate = tail[0] == '-' && tail[5] == '-' && tail[8] == '-' &&
                            isDigit(tail[1]) && isDigit(tail[2]) && isDigit(tail[3]) && isDigit(tail[4]) &&
                            isDigit(tail[6]) && isDigit(tail[7]) && isDigit(tail[9]) && isDigit(tail[10]);
        if (isDate) name.erase(name.size() - DATE_SUFFIX);
    }
    return name.empty() ? filePath.stem().string() : name;
}

std::optional<uint8_t> ProfileService::slotOf(const KeyboardProfile& profile) {
    for (const char* key : {"current", "slot"}) {
        const auto it = profile.m_configData.find(key);
        if (it != profile.m_configData.end() && it->is_number_integer() && it->get<int>() >= 0) {
            return static_cast<uint8_t>(it->get<int>() % 3);
        }
    }
    return std::nullopt;
}

bool ProfileService::reorderProfiles(std::size_t fromIndex, std::size_t toIndex) {
    return m_profileList.moveElement(fromIndex, toIndex);
}

std::optional<KeyboardProfile> ProfileService::findProfileByFileName(const std::string& fileName) const {
    std::optional<KeyboardProfile> match;
    m_profileList.forEach([&](const KeyboardProfile& p) {
        if (!match && p.m_filePath.filename().string() == fileName) {
            match = p;
        }
    });
    return match;
}

bool ProfileService::unassignSlot(uint8_t slot) {
    const uint8_t target = slot % 3;
    bool changed = false;

    m_profileList.forEach([&](KeyboardProfile& p) {
        if (slotOf(p) != target) return;
        p.m_configData.erase("current");
        p.m_configData.erase("slot");
        writeProfileFile(p);
        changed = true;
    });
    return changed;
}

} // namespace kbd