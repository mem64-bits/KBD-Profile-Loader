#pragma once

#include "ui/IProfileGateway.h"

#include <array>
#include <chrono>
#include <optional>

namespace ui {

    // A simulated keyboard with three sample profiles, used by `--demo` to preview and theme the UI
    // without hardware (and by tests as a more realistic fake). Commands take a short, visible amount of
    // time like real HID round-trips do. Not thread-safe; see IProfileGateway.
    class DemoProfileGateway : public IProfileGateway {
    public:
        explicit DemoProfileGateway(std::chrono::milliseconds latency = std::chrono::milliseconds(150));
        ~DemoProfileGateway() override;

        DemoProfileGateway(const DemoProfileGateway&) = delete;
        DemoProfileGateway& operator=(const DemoProfileGateway&) = delete;
        DemoProfileGateway(DemoProfileGateway&&) = delete;
        DemoProfileGateway& operator=(DemoProfileGateway&&) = delete;

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
        void simulateLatency() const;

        std::chrono::milliseconds                 m_latency;
        int                                       m_active{0};
        std::array<SlotInfo, SLOT_COUNT>          m_slots;
        std::array<ActuationSettings, SLOT_COUNT> m_hardware; // what the "keyboard" currently holds
    };

} // namespace ui
