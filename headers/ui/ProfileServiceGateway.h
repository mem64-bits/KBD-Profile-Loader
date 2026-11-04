#pragma once

#include "ui/IProfileGateway.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

struct KeyboardProfile;

namespace kbd {
    class KeyboardDevice;
    class ProfileService;
}

namespace ui {

    // Adapter (Adapter pattern) that implements the ui layer's IProfileGateway on top of the existing
    // kbd::ProfileService / kbd::KeyboardDevice. It adds what the UI needs and the service does not offer:
    // acknowledged commands (the firmware answers every 0xA9 command with a status byte), read-back of the
    // actuation settings, and slot metadata decoded from the profile files.
    //
    // Not thread-safe; see IProfileGateway.
    class ProfileServiceGateway : public IProfileGateway {
    public:
        ProfileServiceGateway(kbd::ProfileService& service, kbd::KeyboardDevice& keyboard);
        ~ProfileServiceGateway() override;

        ProfileServiceGateway(const ProfileServiceGateway&) = delete;
        ProfileServiceGateway& operator=(const ProfileServiceGateway&) = delete;
        ProfileServiceGateway(ProfileServiceGateway&&) = delete;
        ProfileServiceGateway& operator=(ProfileServiceGateway&&) = delete;

        [[nodiscard]] DeviceInfo describeDevice() const override;
        [[nodiscard]] std::array<SlotInfo, SLOT_COUNT> describeSlots() const override;

        [[nodiscard]] int queryActiveSlot() override;
        CommandResult activateSlot(std::uint8_t slot) override;
        CommandResult cycleProfile(bool forward) override;
        CommandResult applyActuation(std::uint8_t slot, const ActuationSettings& settings) override;
        [[nodiscard]] std::optional<ActuationSettings> readActuation(std::uint8_t slot,
                                                                     const std::optional<ActuationSettings>& expected) override;
        CommandResult applyKeyActuation(std::uint8_t slot, const std::vector<KeyPosition>& keys,
                                        const KeyActuation& value) override;
        [[nodiscard]] std::optional<KeyMatrix> readKeyMatrix(std::uint8_t slot) override;
        CommandResult saveSlot(std::uint8_t slot) override;
        CommandResult flashSlotProfile(std::uint8_t slot) override;

        void handleFileCreated(const std::filesystem::path& path) override;
        void handleFileDeleted(const std::filesystem::path& path) override;
        void handleDeviceConnected(kbd::TransportMode mode, const std::string& devicePath) override;
        void handleDeviceDisconnected() override;

        [[nodiscard]] std::vector<LibraryProfileInfo> describeLibrary() const override;
        CommandResult reorderLibrary(std::size_t fromIndex, std::size_t toIndex) override;
        CommandResult assignProfileToSlot(const std::string& fileName, std::uint8_t targetSlot, bool writeNow) override;
        CommandResult clearSlot(std::uint8_t slot) override;

        [[nodiscard]] std::optional<SlotDetails> readSlotDetails(std::uint8_t slot) override;
        CommandResult applyAdvancedKey(std::uint8_t slot, const AdvancedKeyChange& change) override;
        CommandResult applySocd(std::uint8_t slot, std::size_t index, const kbd::SocdPair& pair) override;
        [[nodiscard]] std::optional<ControllerSettings> readControllerSettings() override;
        CommandResult applyControllerMode(std::uint8_t mode) override;
        [[nodiscard]] std::optional<bool> enumeratedXInput() override;
        CommandResult applyCurve(const kbd::ResponseCurve& curve) override;
        [[nodiscard]] std::optional<LiveKeyTravel> readLiveTravel(const KeyPosition& key) override;

    private:
        // Bytes [from, to) of the slot's stored profile (0x12 reads, 26 bytes each), placed at their offsets in a
        // PROFILE_SIZE buffer. nullopt without hardware or when a reply is missing / does not match the request.
        [[nodiscard]] std::optional<std::vector<std::uint8_t>> readProfileRange(std::uint8_t slot, std::size_t from, std::size_t to);

        // Reads reports until one answers `command`; replies to other traffic (for example the device
        // watcher's status pings) are skipped. nullopt on timeout or when the device went away.
        [[nodiscard]] std::optional<std::vector<std::uint8_t>> awaitResponse(std::uint8_t command,
                                                                              std::chrono::milliseconds timeout);

        // Sends `packet` and waits for the keyboard's status byte (0 = success).
        [[nodiscard]] CommandResult sendAcknowledged(const std::vector<std::uint8_t>& packet, std::uint8_t command,
                                                     const std::string& what);

        // Global settings only reach the key scanner when the active profile changes: if `target` is active,
        // switch to a neighbour and back. False when switching back failed.
        bool reloadIfActive(std::uint8_t target);

        // Global + per-key travel, advanced key modes (with their dynamic keystrokes), SOCD pairs and the name of
        // `profile`, then a flash commit (setter replay).
        CommandResult writeProfile(std::uint8_t target, const KeyboardProfile& profile);

        kbd::ProfileService& m_service;
        kbd::KeyboardDevice& m_keyboard;
    };

} // namespace ui
