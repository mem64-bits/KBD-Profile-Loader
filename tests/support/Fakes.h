#pragma once

#include "ui/IProfileCommands.h"
#include "ui/ISystemIntegration.h"
#include "ui/ITrayIcon.h"
#include "ui/IWindowController.h"
#include "ui/TrayIconBase.h"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace test {

    // Records every IProfileCommands call as a short string, e.g. "activate:1", "cycle:next".
    class FakeProfileCommands : public ui::IProfileCommands {
    public:
        void activateSlot(const std::uint8_t slot) override { record("activate:" + std::to_string(slot)); }
        void cycleProfile(const bool forward) override { record(forward ? "cycle:next" : "cycle:prev"); }
        void applyActuation(const std::uint8_t slot, const ui::ActuationSettings& settings, const bool save) override {
            record("apply:" + std::to_string(slot) + ":" + std::to_string(settings.actuationTenths) + (save ? ":save" : ""));
            lastApplied = settings;
        }
        void readActuation(const std::uint8_t slot) override { record("read:" + std::to_string(slot)); }
        void flashSlot(const std::uint8_t slot) override { record("flash:" + std::to_string(slot)); }
        void refresh() override { record("refresh"); }

        [[nodiscard]] std::vector<std::string> calls() const {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_calls;
        }

        ui::ActuationSettings lastApplied;

    private:
        void record(std::string call) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_calls.push_back(std::move(call));
        }

        mutable std::mutex       m_mutex;
        std::vector<std::string> m_calls;
    };

    class FakeSystemIntegration : public ui::ISystemIntegration {
    public:
        void openProfilesFolder() override { record("profiles"); }
        void openConfigFile() override { record("config"); }
        void openLogFile() override { record("log"); }
        [[nodiscard]] bool isAutostartEnabled() const override { return autostart.load(); }
        bool setAutostartEnabled(const bool enabled) override {
            record(enabled ? "autostart:on" : "autostart:off");
            autostart = enabled;
            return true;
        }

        [[nodiscard]] std::vector<std::string> calls() const {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_calls;
        }

        std::atomic<bool> autostart{false};

    private:
        void record(std::string call) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_calls.push_back(std::move(call));
        }

        mutable std::mutex       m_mutex;
        std::vector<std::string> m_calls;
    };

    class FakeWindowController : public ui::IWindowController {
    public:
        void showDashboard() override { ++shows; visible = true; }
        void hideDashboard() override { ++hides; visible = false; }
        void toggleDashboard() override { ++toggles; visible = !visible; }
        [[nodiscard]] bool isDashboardVisible() const noexcept override { return visible; }

        std::atomic<int>  shows{0};
        std::atomic<int>  hides{0};
        std::atomic<int>  toggles{0};
        std::atomic<bool> visible{false};
    };

    // A TrayIconBase backend that draws nothing but counts what it was asked to draw and lets a test
    // simulate the user clicking the icon or choosing a menu entry.
    class FakeTrayIcon : public ui::TrayIconBase {
    public:
        bool start() override {
            started = startResult;
            return startResult;
        }
        void stop() override { started = false; }
        [[nodiscard]] bool isRunning() const noexcept override { return started; }
        [[nodiscard]] std::string_view backendName() const noexcept override { return "fake"; }

        // --- simulate the user ---
        void click() { notifyActivated(); }
        void middleClick() { notifySecondaryActivated(); }
        void choose(const int itemId) { notifyMenuItemSelected(itemId); }

        // --- inspect what was shown ---
        [[nodiscard]] ui::TrayMenu menu() const { return state().menu; }
        [[nodiscard]] std::string title() const { return state().title; }
        [[nodiscard]] std::string description() const { return state().description; }
        [[nodiscard]] std::vector<ui::TrayImage> icons() const { return state().icons; }

        bool              startResult = true;
        std::atomic<bool> started{false};
        std::atomic<int>  menuUpdates{0};
        std::atomic<int>  iconUpdates{0};
        std::atomic<int>  tooltipUpdates{0};

    private:
        void onStateChanged(const unsigned changes) override {
            if (changes & MenuChanged) ++menuUpdates;
            if (changes & IconChanged) ++iconUpdates;
            if (changes & TooltipChanged) ++tooltipUpdates;
        }
    };

} // namespace test
