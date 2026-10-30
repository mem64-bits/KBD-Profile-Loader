#pragma once

#include "ui/IProfileGateway.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

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
        CommandResult saveSlot(std::uint8_t slot) override;
        CommandResult flashSlotProfile(std::uint8_t slot) override;

        void handleFileCreated(const std::filesystem::path& path) override;
        void handleFileDeleted(const std::filesystem::path& path) override;
        void handleDeviceConnected(kbd::TransportMode mode, const std::string& devicePath) override;
        void handleDeviceDisconnected() override;

    private:
        // Reads reports until one answers `command`; replies to other traffic (for example the device
        // watcher's status pings) are skipped. nullopt on timeout or when the device went away.
        [[nodiscard]] std::optional<std::vector<std::uint8_t>> awaitResponse(std::uint8_t command,
                                                                              std::chrono::milliseconds timeout);

        // Sends `packet` and waits for the keyboard's status byte (0 = success).
        [[nodiscard]] CommandResult sendAcknowledged(const std::vector<std::uint8_t>& packet, std::uint8_t command,
                                                     const std::string& what);

        kbd::ProfileService& m_service;
        kbd::KeyboardDevice& m_keyboard;
    };

} // namespace ui
