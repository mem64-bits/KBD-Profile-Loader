#pragma once

#include "files/IFileObserver.h"
#include "kbd/IDeviceObserver.h"
#include "dsa/DLL.h"
#include "kbd/KeyboardProfile.h"

#include <filesystem>
#include <cstdint>
#include <string>

namespace fs = std::filesystem;

namespace kbd {

    class KeyboardDevice;

    class ProfileService : public Files::IFileObserver, public IDeviceObserver {
    public:
        explicit ProfileService(KeyboardDevice& keyboard);

        void loadProfileFromFile(const fs::path& filePath);
        void loadAllFromDirectory(const fs::path& folder);

        void onFileCreated(const fs::path& path) override;
        void onFileDeleted(const fs::path& path) override;

        void onDeviceConnected(TransportMode mode, const std::string& path) override;
        void onDeviceDisconnected() override;

        void cycleForward();
        void cycleBackward();

        bool applyProfile(const KeyboardProfile& profile, bool flashFullMemory = false);
        bool switchHardwareSlot(uint8_t slotIndex);

        // Streams 34 chunks (0x12) AND commits via AMC_SAVE_PROFILE (0x1F)
        bool flashProfileMemory(uint8_t slotIndex, const KeyboardProfile& profile) const;

        // Fast sub-millisecond tuning via AMC_SET_TRAVAL (0x14)
        bool applyFastTravel(uint8_t slotIndex, bool rapidTrigger, uint8_t actPointTenths, uint8_t pressSensTenths, uint8_t releaseSensTenths) const;

        // Commits custom name string directly to MCU table via AMC_SET_PROFILE_NAME (0x13)
        bool syncProfileNameToHardware(uint8_t slotIndex, const std::string& name) const;

        // Add to public methods in headers/files/ProfileService.h:

        // Reassigns a profile to a specific hardware slot (0, 1, 2) and saves the change to the .json file on disk
        bool setProfileSlotAndSave(KeyboardProfile& profile, uint8_t slotIndex);

        int queryActiveHardwareSlot();
        [[nodiscard]] std::string getProfileNameForSlot(uint8_t slotIndex) const;

        [[nodiscard]] std::size_t profileCount() const noexcept;
        [[nodiscard]] const KeyboardProfile& getCurrentProfile() const;

    private:
        uint8_t resolveTargetSlot(const KeyboardProfile& profile) const;

        dsa::DLL<KeyboardProfile> m_profileList;
        KeyboardDevice&           m_keyboard;
    };

} // namespace kbd