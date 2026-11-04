#pragma once

#include "files/IFileObserver.h"
#include "kbd/IDeviceObserver.h"
#include "dsa/DLL.h"
#include "kbd/KeyboardProfile.h"

#include <filesystem>
#include <cstdint>
#include <optional>
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

        // Streams 34 chunks (0x12) and commits via AMC_SAVE_PROFILE (0x1F).
        // NOTE: the firmware treats 0x12 as a read, so this does not change the stored profile. The UI writes
        // profiles through ui::ProfileServiceGateway::flashSlotProfile, which replays the setters instead.
        bool flashProfileMemory(uint8_t slotIndex, const KeyboardProfile& profile) const;

        // Fast sub-millisecond tuning via AMC_SET_TRAVAL (0x14)
        bool applyFastTravel(uint8_t slotIndex, bool rapidTrigger, uint8_t actPointTenths, uint8_t pressSensTenths, uint8_t releaseSensTenths) const;

        // Commits custom name string directly to MCU table via AMC_SET_PROFILE_NAME (0x13)
        bool syncProfileNameToHardware(uint8_t slotIndex, const std::string& name) const;

        // Makes the file `fileName` the profile of `slotIndex` (0, 1, 2) by writing its "current" field.
        // Any other file that claimed the slot loses its assignment, so a slot always has at most one profile.
        // The list order is kept. False when no loaded profile comes from `fileName` or a file write failed.
        bool assignSlot(const std::string& fileName, uint8_t slotIndex);

        int queryActiveHardwareSlot();
        [[nodiscard]] std::string getProfileNameForSlot(uint8_t slotIndex) const;

        // Copy of the profile assigned to a hardware slot (0, 1, 2), or nullopt if no profile file claims it
        [[nodiscard]] std::optional<KeyboardProfile> getProfileForSlot(uint8_t slotIndex) const;

        [[nodiscard]] std::size_t profileCount() const noexcept;
        [[nodiscard]] const KeyboardProfile& getCurrentProfile() const;

        // Visits every loaded profile in list (= cycling) order.
        template <typename Callback>
        void forEachProfile(Callback&& func) const {
            m_profileList.forEach(std::forward<Callback>(func));
        }

        // Moves the profile at `fromIndex` to `toIndex` in the list (in memory only; a restart sorts by slot).
        bool reorderProfiles(std::size_t fromIndex, std::size_t toIndex);

        // Finds a profile by its file name (e.g. "HEProfile-Tekken 8-2026-10-28.json")
        [[nodiscard]] std::optional<KeyboardProfile> findProfileByFileName(const std::string& fileName) const;

        // Removes the slot assignment from every file that claims `slot`; false when none did.
        bool unassignSlot(uint8_t slot);

        // The slot a profile file is assigned to ("current", or the older "slot" key), nullopt = library only.
        [[nodiscard]] static std::optional<uint8_t> slotOf(const KeyboardProfile& profile);

        // Name to show for a file without a "name" field. Keychron Launcher exports are called
        // "HEProfile-<name>-<yyyy-mm-dd>.json" and carry no name inside, so that part is used.
        [[nodiscard]] static std::string displayNameFor(const fs::path& filePath);

    private:
        bool cycle(bool forward);
        static bool writeProfileFile(const KeyboardProfile& profile);

        dsa::DLL<KeyboardProfile> m_profileList;
        KeyboardDevice&           m_keyboard;
    };

} // namespace kbd