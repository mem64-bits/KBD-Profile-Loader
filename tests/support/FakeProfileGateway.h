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
        std::optional<ui::ActuationSettings>     hardware;           // what readActuation() reports
        std::optional<ui::ActuationSettings>     firmwareOverride;   // if set, apply stores this instead of the request
        std::optional<ui::ActuationSettings>     lastExpected;       // `expected` argument of the latest readActuation()
        bool                                     throwOnActivate = false;
        bool                                     throwOnDescribeSlots = false;
        std::function<void()>                    onActivate;         // runs inside activateSlot (may block)

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
