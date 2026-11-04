#pragma once

#include "ui/IProfileGateway.h"

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace ui {

    // A simulated keyboard with a small profile library (three files assigned to the slots, two library-only),
    // used by `--demo` to preview and theme the UI without hardware (and by tests as a more realistic fake).
    // Commands take a short, visible amount of time like real HID round-trips do. Not thread-safe; see
    // IProfileGateway.
    class DemoProfileGateway : public IProfileGateway {
    public:
        explicit DemoProfileGateway(std::chrono::milliseconds latency = std::chrono::milliseconds(150), int activeSlot = 0);
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
        // Simulated: every watched key goes up and down on its own rhythm.
        [[nodiscard]] std::optional<LiveKeyTravel> readLiveTravel(const KeyPosition& key) override;

    private:
        struct DemoProfile {
            std::string                 name;
            std::string                 fileName;
            std::optional<std::uint8_t> slot;
            ActuationSettings           global;
            KeyMatrix                   keys;
            ProfileExtras               extras{};
            SlotDetails                 details{};
        };

        void simulateLatency() const;
        [[nodiscard]] const DemoProfile* profileForSlot(std::size_t slot) const;

        std::chrono::milliseconds                 m_latency;
        int                                       m_active{0};
        std::vector<DemoProfile>                  m_library;
        std::array<ActuationSettings, SLOT_COUNT> m_hardware;     // what the "keyboard" currently holds
        std::array<KeyMatrix, SLOT_COUNT>         m_hardwareKeys;
        std::array<SlotDetails, SLOT_COUNT>       m_hardwareDetails;
        std::uint8_t                              m_controllerMode{kbd::ControllerMode::XINPUT | kbd::ControllerMode::TYPING};
        bool                                      m_enumeratedXInput{true}; // what the simulated PC saw at start-up
        kbd::ResponseCurve                        m_curve{kbd::DEFAULT_CURVE};
        std::chrono::steady_clock::time_point     m_start{std::chrono::steady_clock::now()};
    };

} // namespace ui
