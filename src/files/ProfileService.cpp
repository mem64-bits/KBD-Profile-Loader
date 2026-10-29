#include "files/ProfileService.h"
#include "kbd/KeyboardDevice.h"
#include "kbd/KeychronProtocol.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>

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
        std::string name = data.value("name", filePath.stem().string());

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
                std::string name = data.value("name", entry.path().stem().string());
                tempProfiles.emplace_back(name, entry.path(), std::move(data));
            } catch (...) {}
        }
    }

    // 1. Sort profiles strictly by hardware slot index (Slot 1 -> Slot 2 -> Slot 3)
    std::ranges::sort(tempProfiles, [this](const KeyboardProfile& a, const KeyboardProfile& b) {
        return resolveTargetSlot(a) < resolveTargetSlot(b);
    });

    // 2. Add them in sorted order to the doubly linked list
    for (auto& profile : tempProfiles) {
        if (m_profileList.contains(profile)) {
            m_profileList.remove(profile);
        }
        std::cout << "[ProfileService] Loaded profile (Slot " << static_cast<int>( resolveTargetSlot(profile) ) + 1 << "): "
                << profile.m_name << '\n';
        m_profileList.addLast(profile);
    }
}

void ProfileService::onFileCreated(const fs::path& path) {
    loadProfileFromFile(path);
}

void ProfileService::onFileDeleted(const fs::path& path) {
    KeyboardProfile dummy(path.stem().string(), path, {});
    m_profileList.remove(dummy);
    std::cout << "[ProfileService] Removed profile: " << path.stem().string() << '\n';
}

void ProfileService::onDeviceConnected(TransportMode mode, const std::string& path) {
    std::cout << "\n[Hardware Auto-Sync] Reconnected via "
            << (mode == TransportMode::Wired ? "Wired USB-C" : "2.4G Wireless Dongle")
            << ". Re-applying active profile...\nkbd> " << std::flush;

    if (m_profileList.size() > 0) {
        applyProfile(m_profileList.getCurrent(), false);
    }
}

void ProfileService::onDeviceDisconnected() {
    std::cout << "\n[Hardware Notice] Keyboard disconnected (USB unplugged or wireless out of range).\nkbd> " << std::flush;
}

void ProfileService::cycleForward() {
    if (m_profileList.size() == 0) {
        std::cout << "[ProfileService] No profiles loaded.\n";
        return;
    }
    const auto& profile = m_profileList.cycleNext();
    applyProfile(profile, false);
}

void ProfileService::cycleBackward() {
    if (m_profileList.size() == 0) {
        std::cout << "[ProfileService] No profiles loaded.\n";
        return;
    }
    const auto& profile = m_profileList.cyclePrev();
    applyProfile(profile, false);
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

    uint8_t targetSlot = resolveTargetSlot(profile);

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
        m_profileList.forEach([&](const KeyboardProfile& p) {
            if (resolveTargetSlot(p) == confirmedSlot) {
                m_profileList.setCursorTo(p);
            }
        });
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
        std::this_thread::sleep_for(50ms); // Hardware SPI write settling delay

        std::cout << "[Flash Engine] Successfully flashed & saved Slot " << static_cast<int>( slotIndex ) + 1 << " permanently.\n";
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

bool ProfileService::setProfileSlotAndSave(KeyboardProfile& profile, uint8_t slotIndex) {
    uint8_t targetSlot = slotIndex % 3;

    // 1. Update the JSON metadata
    profile.m_configData["current"] = targetSlot;

    // 2. Write the updated JSON back to disk
    try {
        std::ofstream file(profile.m_filePath, std::ios::trunc);
        if (file.is_open()) {
            file << profile.m_configData.dump(4);
            std::cout << "[ProfileService] Updated " << profile.m_filePath.filename().string()
                    << " -> assigned to Slot " << (int)targetSlot + 1 << " (index " << (int)targetSlot << ")\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "[ProfileService] Failed to write updated slot to disk: " << e.what() << '\n';
        return false;
    }

    // 3. Re-sort the linked list so slot ordering remains 1 -> 2 -> 3
    std::vector<KeyboardProfile> tempProfiles;
    m_profileList.forEach([&](const KeyboardProfile& p) {
        tempProfiles.push_back(p);
    });

    std::sort(tempProfiles.begin(), tempProfiles.end(), [this](const KeyboardProfile& a, const KeyboardProfile& b) {
        return resolveTargetSlot(a) < resolveTargetSlot(b);
    });

    while (m_profileList.size() > 0) {
        m_profileList.remove(m_profileList.getCurrent());
    }

    for (auto& p : tempProfiles) {
        m_profileList.addLast(std::move(p));
    }

    m_profileList.setCursorTo(profile);
    return true;
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

    std::vector<uint8_t> response;
    int bytes = m_keyboard.readReport(response, 150);

    if (bytes > 0 && response.size() >= 3) {
        return static_cast<int>(response[2] % 3);
    }
    return -1;
}

    std::string ProfileService::getProfileNameForSlot(uint8_t slotIndex) const {
        uint8_t target = slotIndex % 3;
        std::string matchedName;

        m_profileList.forEach([&](const KeyboardProfile& p) {
            if (resolveTargetSlot(p) == target && matchedName.empty()) {
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

std::size_t ProfileService::profileCount() const noexcept {
    return m_profileList.size();
}

const KeyboardProfile& ProfileService::getCurrentProfile() const {
    return m_profileList.getCurrent();
}

    uint8_t ProfileService::resolveTargetSlot(const KeyboardProfile& profile) const {
        if (profile.m_configData.contains("current") && profile.m_configData["current"].is_number()) {
            return profile.m_configData["current"].get<uint8_t>() % 3;
        }
        if (profile.m_configData.contains("slot") && profile.m_configData["slot"].is_number()) {
            return profile.m_configData["slot"].get<uint8_t>() % 3;
        }
        return 0;
    }

} // namespace kbd