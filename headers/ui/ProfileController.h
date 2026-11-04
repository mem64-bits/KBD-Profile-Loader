#pragma once

#include "files/IFileObserver.h"
#include "kbd/IDeviceObserver.h"
#include "ui/AppModel.h"
#include "ui/IProfileCommands.h"
#include "ui/IProfileGateway.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace ui {

    // Executes every profile / keyboard operation on ONE background thread and publishes the results
    // to the AppModel.
    //
    //  - The UI, the tray, hotkeys and the CLI/IPC server only enqueue work (IProfileCommands), so the
    //    render loop never blocks on a HID round-trip and two HID transactions can never interleave.
    //  - It also observes the file and device watchers: their events are queued as well, which means the
    //    wrapped ProfileService is only ever touched from this thread once start() has been called.
    class ProfileController : public IProfileCommands, public Files::IFileObserver, public kbd::IDeviceObserver {
    public:
        static constexpr std::chrono::milliseconds DEFAULT_POLL_INTERVAL{2000};

        ProfileController(IProfileGateway& gateway, AppModel& model,
                          std::chrono::milliseconds pollInterval = DEFAULT_POLL_INTERVAL);
        ~ProfileController() override;

        ProfileController(const ProfileController&) = delete;
        ProfileController& operator=(const ProfileController&) = delete;
        ProfileController(ProfileController&&) = delete;
        ProfileController& operator=(ProfileController&&) = delete;

        // Starts the worker and schedules an initial refresh. Work queued before start() is kept.
        void start();
        // Finishes the running task, drops queued ones and joins. Later commands are ignored.
        void stop();

        // While enabled the active slot is re-read every poll interval, so profile changes made with
        // the keyboard's own shortcuts show up. Meant to be on only while a window is open.
        void setPollingEnabled(bool enabled);

        // --- IProfileCommands ---
        void activateSlot(std::uint8_t slot) override;
        void cycleProfile(bool forward) override;
        void applyActuation(std::uint8_t slot, const ActuationSettings& settings, bool saveToFlash) override;
        void readActuation(std::uint8_t slot) override;
        void applyKeyActuation(std::uint8_t slot, std::vector<KeyPosition> keys, const KeyActuation& value,
                               bool saveToFlash) override;
        void flashSlot(std::uint8_t slot) override;
        void refresh() override;
        void reorderLibrary(std::size_t fromIndex, std::size_t toIndex) override;
        void assignProfileToSlot(const std::string& fileName, std::uint8_t targetSlot, bool writeNow) override;
        void clearSlot(std::uint8_t slot) override;
        void readSlotDetails(std::uint8_t slot) override;
        void applyAdvancedKeys(std::uint8_t slot, std::vector<AdvancedKeyChange> changes, bool saveToFlash) override;
        void applySocdPairs(std::uint8_t slot, std::vector<kbd::SocdPair> pairs, bool saveToFlash) override;
        void readControllerSettings() override;
        void applyControllerMode(std::uint8_t mode) override;
        void setAutoControllerMode(bool enabled) override;
        [[nodiscard]] bool autoControllerMode() const noexcept override;
        void applyCurve(const kbd::ResponseCurve& curve) override;
        void watchLiveTravel(std::vector<KeyPosition> keys) override;
        [[nodiscard]] std::vector<LiveKeyTravel> liveTravel() const override;

        // Live travel is polled this often while the window is open and keys are watched.
        static constexpr std::chrono::milliseconds LIVE_POLL_INTERVAL{16};

        // --- Files::IFileObserver (called on the directory watcher thread) ---
        void onFileCreated(const std::filesystem::path& path) override;
        void onFileDeleted(const std::filesystem::path& path) override;

        // --- kbd::IDeviceObserver (called on the device watcher thread) ---
        void onDeviceConnected(kbd::TransportMode mode, const std::string& path) override;
        void onDeviceDisconnected() override;

    private:
        using Task = std::function<void()>;

        void post(Task task);
        void workerLoop(const std::stop_token& stopToken);

        // Marks the model busy, runs `body` (which must finish with endBusy) and guarantees the busy flag
        // is cleared with an error status even if `body` throws.
        void runBusy(const std::string& busyText, const std::function<void()>& body);
        void runCommand(const std::string& busyText, const std::function<CommandResult()>& command);
        void runApplyActuation(std::uint8_t slot, const ActuationSettings& settings, bool saveToFlash);
        void runApplyKeyActuation(std::uint8_t slot, const std::vector<KeyPosition>& keys, const KeyActuation& value,
                                  bool saveToFlash);
        void saveIfRequested(std::uint8_t slot, bool saveToFlash, StatusSeverity& severity, std::string& message);
        void runReadActuation(std::uint8_t slot);
        void runFlashSlot(std::uint8_t slot);

        void refreshState(bool queryHardware);
        void pollActiveSlot();
        // Automatic gamepad mode for the slot that just became active (worker thread).
        void syncControllerMode(int activeSlot);
        void pollLiveTravel();
        void runApplyAdvancedKeys(std::uint8_t slot, const std::vector<AdvancedKeyChange>& changes, bool saveToFlash);
        void runApplySocd(std::uint8_t slot, const std::vector<kbd::SocdPair>& pairs, bool saveToFlash);
        void storeDetails(std::uint8_t slot);
        void beginBusy(const std::string& text);
        void endBusy(StatusSeverity severity, const std::string& text);

        IProfileGateway&          m_gateway;
        AppModel&                 m_model;
        std::chrono::milliseconds m_pollInterval;

        std::mutex                  m_mutex;     // guards m_tasks and m_stopped
        std::condition_variable_any m_wakeup;
        std::deque<Task>            m_tasks;
        bool                        m_stopped{false};
        std::atomic<bool>           m_polling{false};

        mutable std::mutex          m_liveMutex;  // guards the two below
        std::vector<KeyPosition>    m_liveKeys;
        std::vector<LiveKeyTravel>  m_liveReadings;
        std::atomic<bool>           m_liveWanted{false};
        std::atomic<bool>           m_reschedule{false};
        std::chrono::steady_clock::time_point m_lastSlotPoll{};
        std::atomic<bool> m_autoControllerMode{true};
        int m_lastSyncedSlot{-1};

        std::jthread                m_thread;
    };

} // namespace ui
