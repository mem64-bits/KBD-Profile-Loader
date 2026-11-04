#pragma once

#include "ui/IProfileGateway.h"

#include <algorithm>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace test {

    // Scriptable IProfileGateway for controller tests. Configure the public fields before starting the
    // controller; read the call log with calls() / countOf() afterwards (both are thread-safe).
    class FakeProfileGateway : public ui::IProfileGateway {
    public:
        // --- scripted behaviour ---------------------------------------------------------------
        ui::DeviceInfo                           device{true, kbd::TransportMode::Wired, "fake-hid-path"};
        std::array<ui::SlotInfo, ui::SLOT_COUNT> slots{};
        int                                      activeSlot = 0;
        ui::CommandResult                        activateResult{true, "Slot active"};
        ui::CommandResult                        cycleResult{true, "Cycled"};
        ui::CommandResult                        applyResult{true, "Applied"};
        ui::CommandResult                        saveResult{true, "Saved"};
        ui::CommandResult                        flashResult{true, "Flashed"};
        bool                                     readSupported = true;
        ui::CommandResult                        keyApplyResult{true, "Keys applied"};
        ui::CommandResult                        assignResult{true, "Assigned"};
        ui::CommandResult                        clearResult{true, "Cleared"};
        ui::CommandResult                        reorderResult{true, "Reordered"};
        std::vector<ui::LibraryProfileInfo>      library;
        ui::KeyMatrix                            hardwareKeys;       // what readKeyMatrix() reports
        std::optional<ui::KeyActuation>          keyFirmwareOverride; // if set, key apply stores this instead
        std::optional<ui::ActuationSettings>     hardware;           // what readActuation() reports
        std::optional<ui::ActuationSettings>     firmwareOverride;   // if set, apply stores this instead of the request
        std::optional<ui::ActuationSettings>     lastExpected;       // `expected` argument of the latest readActuation()
        bool                                     throwOnActivate = false;
        bool                                     throwOnDescribeSlots = false;
        std::function<void()>                    onActivate;         // runs inside activateSlot (may block)
        ui::SlotDetails                          hardwareDetails;    // what readSlotDetails() reports (and edits store into)
        bool                                     detailsSupported = true;
        std::optional<std::uint8_t>              keyModeOverride;    // if set, advanced-key edits store this mode instead
        ui::CommandResult                        advancedResult{true, "Key updated"};
        ui::CommandResult                        socdResult{true, "SOCD accepted"};
        ui::ControllerSettings                   controller{std::uint8_t{kbd::ControllerMode::XINPUT}, kbd::DEFAULT_CURVE, std::nullopt, false};
        std::optional<bool>                      enumerated;         // enumeratedXInput(): what the "PC" enumerated
        bool                                     controllerSupported = true;
        std::vector<ui::LiveKeyTravel>           liveReadings;       // readLiveTravel() answers from these

        FakeProfileGateway() {
            for (std::size_t i = 0; i < slots.size(); ++i) slots[i].name = ui::slotLabel(i);
        }

        // --- call log -------------------------------------------------------------------------
        [[nodiscard]] std::vector<std::string> calls() const {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            return m_calls;
        }

        [[nodiscard]] int countOf(const std::string& prefix) const {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            return static_cast<int>(std::count_if(m_calls.begin(), m_calls.end(), [&](const std::string& call) {
                return call.rfind(prefix, 0) == 0;
            }));
        }

        [[nodiscard]] std::vector<std::thread::id> threadIds() const {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            return m_threads;
        }

        void setActiveSlot(const int slot) {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            activeSlot = slot;
        }

        void setDevice(ui::DeviceInfo value) {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            device = std::move(value);
        }

        // --- IProfileGateway ------------------------------------------------------------------
        [[nodiscard]] ui::DeviceInfo describeDevice() const override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            return device;
        }

        [[nodiscard]] std::array<ui::SlotInfo, ui::SLOT_COUNT> describeSlots() const override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            if (throwOnDescribeSlots) throw std::runtime_error("describeSlots failed");
            return slots;
        }

        int queryActiveSlot() override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            record("query");
            return activeSlot;
        }

        ui::CommandResult activateSlot(const std::uint8_t slot) override {
            record("activate:" + std::to_string(slot));
            if (onActivate) onActivate();
            if (throwOnActivate) throw std::runtime_error("boom");

            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            if (activateResult.ok) activeSlot = slot;
            return activateResult;
        }

        ui::CommandResult cycleProfile(const bool forward) override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            record(forward ? "cycle:next" : "cycle:prev");
            return cycleResult;
        }

        ui::CommandResult applyActuation(const std::uint8_t slot, const ui::ActuationSettings& settings) override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            record("apply:" + std::to_string(slot) + ":" + std::to_string(settings.actuationTenths) + ":" +
                   (settings.rapidTrigger ? "rt" : "static") + ":" + std::to_string(settings.pressTenths) + ":" +
                   std::to_string(settings.releaseTenths));
            if (applyResult.ok) hardware = firmwareOverride ? firmwareOverride : std::optional<ui::ActuationSettings>(settings);
            return applyResult;
        }

        [[nodiscard]] std::optional<ui::ActuationSettings> readActuation(const std::uint8_t slot,
                                                                         const std::optional<ui::ActuationSettings>& expected) override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            record("read:" + std::to_string(slot));
            lastExpected = expected;
            return readSupported ? hardware : std::nullopt;
        }

        ui::CommandResult applyKeyActuation(const std::uint8_t slot, const std::vector<ui::KeyPosition>& keys,
                                            const ui::KeyActuation& value) override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            record("applyKeys:" + std::to_string(slot) + ":" + std::to_string(keys.size()) + ":" +
                   std::to_string(value.mode) + ":" + std::to_string(value.actuationTenths));
            if (keyApplyResult.ok) {
                for (const auto& key : keys) hardwareKeys.at(key) = keyFirmwareOverride.value_or(value);
            }
            return keyApplyResult;
        }

        [[nodiscard]] std::optional<ui::KeyMatrix> readKeyMatrix(const std::uint8_t slot) override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            record("readKeys:" + std::to_string(slot));
            return readSupported ? std::optional<ui::KeyMatrix>(hardwareKeys) : std::nullopt;
        }

        ui::CommandResult saveSlot(const std::uint8_t slot) override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            record("save:" + std::to_string(slot));
            return saveResult;
        }

        ui::CommandResult flashSlotProfile(const std::uint8_t slot) override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            record("flash:" + std::to_string(slot));
            return flashResult;
        }

        void handleFileCreated(const std::filesystem::path& path) override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            record("fileCreated:" + path.filename().string());
        }

        void handleFileDeleted(const std::filesystem::path& path) override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            record("fileDeleted:" + path.filename().string());
        }

        void handleDeviceConnected(const kbd::TransportMode mode, const std::string& devicePath) override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            record("deviceConnected:" + std::string(ui::transportName(mode)) + ":" + devicePath);
        }

        void handleDeviceDisconnected() override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            record("deviceDisconnected");
        }

        [[nodiscard]] std::vector<ui::LibraryProfileInfo> describeLibrary() const override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            return library;
        }

        ui::CommandResult reorderLibrary(const std::size_t fromIndex, const std::size_t toIndex) override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            record("reorder:" + std::to_string(fromIndex) + ":" + std::to_string(toIndex));
            return reorderResult;
        }

        ui::CommandResult assignProfileToSlot(const std::string& fileName, const std::uint8_t slot, const bool writeNow) override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            record("assign:" + fileName + ":" + std::to_string(slot) + (writeNow ? ":write" : ""));
            return assignResult;
        }

        ui::CommandResult clearSlot(const std::uint8_t slot) override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            record("clear:" + std::to_string(slot));
            return clearResult;
        }

        std::optional<ui::SlotDetails> readSlotDetails(const std::uint8_t slot) override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            record("readDetails:" + std::to_string(slot));
            if (!detailsSupported) return std::nullopt;
            return hardwareDetails;
        }

        ui::CommandResult applyAdvancedKey(const std::uint8_t slot, const ui::AdvancedKeyChange& change) override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            record("advanced:" + std::to_string(slot) + ":" + std::to_string(change.key.row) + "," + std::to_string(change.key.col) + ":" +
                   std::to_string(static_cast<int>(change.kind)) + ":" + std::to_string(change.index));
            if (!advancedResult.ok) return advancedResult;
            ui::KeyAdvanced stored;
            switch (change.kind) {
                case ui::AdvancedKeyChange::Kind::Gamepad:          stored = {kbd::AdvancedMode::GAMEPAD, change.index}; break;
                case ui::AdvancedKeyChange::Kind::Toggle:           stored = {kbd::AdvancedMode::TOGGLE, 0}; break;
                case ui::AdvancedKeyChange::Kind::DynamicKeystroke:
                    stored = {kbd::AdvancedMode::DKS, change.index};
                    hardwareDetails.okmc[change.index % hardwareDetails.okmc.size()] = change.okmc;
                    break;
                case ui::AdvancedKeyChange::Kind::Clear:            break;
            }
            if (keyModeOverride) stored.mode = *keyModeOverride;
            hardwareDetails.set(change.key, stored);
            return advancedResult;
        }

        ui::CommandResult applySocd(const std::uint8_t slot, const std::size_t index, const kbd::SocdPair& pair) override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            record("socd:" + std::to_string(slot) + ":" + std::to_string(index) + ":" + std::to_string(pair.type));
            if (socdResult.ok && index < hardwareDetails.socd.size()) hardwareDetails.socd[index] = pair;
            return socdResult;
        }

        std::optional<ui::ControllerSettings> readControllerSettings() override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            record("readController");
            if (!controllerSupported) return std::nullopt;
            return controller;
        }

        std::optional<bool> enumeratedXInput() override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            return enumerated;
        }

        ui::CommandResult applyControllerMode(const std::uint8_t mode) override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            record("controllerMode:" + std::to_string(mode));
            controller.mode = mode;
            return {true, "Mode accepted"};
        }

        ui::CommandResult applyCurve(const kbd::ResponseCurve& curve) override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            record("curve:" + std::to_string(curve[1].x) + "," + std::to_string(curve[1].y));
            controller.curve = curve;
            return {true, "Curve accepted"};
        }

        std::optional<ui::LiveKeyTravel> readLiveTravel(const ui::KeyPosition& key) override {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            record("live:" + std::to_string(key.row) + "," + std::to_string(key.col));
            for (const ui::LiveKeyTravel& reading : liveReadings) {
                if (reading.key == key) return reading;
            }
            return std::nullopt;
        }

    private:
        // Always locks itself; the mutex is recursive so callers that already hold it may call this too.
        void record(const std::string& call) {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            m_calls.push_back(call);
            m_threads.push_back(std::this_thread::get_id());
        }

        mutable std::recursive_mutex m_mutex;
        std::vector<std::string>     m_calls;
        std::vector<std::thread::id> m_threads;
    };

} // namespace test
