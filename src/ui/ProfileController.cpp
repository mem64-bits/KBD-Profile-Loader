#include "ui/ProfileController.h"
#include "files/Logger.h"

#include <exception>
#include <optional>
#include <utility>

namespace ui {

namespace {

    CommandResult guarded(const std::function<CommandResult()>& command) {
        try {
            return command();
        } catch (const std::exception& e) {
            return {false, std::string("Unexpected error: ") + e.what()};
        } catch (...) {
            return {false, "Unexpected error."};
        }
    }

    std::optional<ActuationSettings> guardedRead(const std::function<std::optional<ActuationSettings>()>& read) {
        try {
            return read();
        } catch (const std::exception& e) {
            LOG_WARN("UI", std::string("Reading the keyboard failed: ") + e.what());
        } catch (...) {
            LOG_WARN("UI", "Reading the keyboard failed.");
        }
        return std::nullopt;
    }

    void guardedVoid(const std::function<void()>& action) {
        try {
            action();
        } catch (const std::exception& e) {
            LOG_WARN("UI", std::string("Event handling failed: ") + e.what());
        } catch (...) {
            LOG_WARN("UI", "Event handling failed.");
        }
    }

    std::uint8_t clampSlot(const std::uint8_t slot) {
        return static_cast<std::uint8_t>(slot % SLOT_COUNT);
    }

} // namespace

ProfileController::ProfileController(IProfileGateway& gateway, AppModel& model, const std::chrono::milliseconds pollInterval)
    : m_gateway(gateway), m_model(model), m_pollInterval(pollInterval)
{}

ProfileController::~ProfileController() {
    stop();
}

void ProfileController::start() {
    if (m_thread.joinable()) return;

    m_thread = std::jthread([this](const std::stop_token& stopToken) { workerLoop(stopToken); });
    refresh();
}

void ProfileController::stop() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopped = true;
        m_tasks.clear();
    }

    if (m_thread.joinable()) {
        m_thread.request_stop();
        m_wakeup.notify_all();
        m_thread.join();
    }
}

void ProfileController::setPollingEnabled(const bool enabled) {
    const bool wasEnabled = m_polling.exchange(enabled);
    if (enabled && !wasEnabled) refresh(); // opening a window: show fresh state right away
}

// ---------------------------------------------------------------------------
// Worker
// ---------------------------------------------------------------------------

void ProfileController::post(Task task) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopped) return;
        m_tasks.push_back(std::move(task));
    }
    m_wakeup.notify_one();
}

void ProfileController::workerLoop(const std::stop_token& stopToken) {
    std::unique_lock<std::mutex> lock(m_mutex);

    while (!stopToken.stop_requested()) {
        if (m_tasks.empty()) {
            const bool hasWork = m_wakeup.wait_for(lock, stopToken, m_pollInterval, [this] { return !m_tasks.empty(); });
            if (stopToken.stop_requested()) break;

            if (!hasWork) { // timed out with nothing queued
                if (m_polling.load()) {
                    lock.unlock();
                    guardedVoid([this] { pollActiveSlot(); });
                    lock.lock();
                }
                continue;
            }
        }

        Task task = std::move(m_tasks.front());
        m_tasks.pop_front();

        lock.unlock();
        guardedVoid(task);
        lock.lock();
    }
}

void ProfileController::pollActiveSlot() {
    if (!m_gateway.describeDevice().connected) return; // reconnects arrive through the device watcher
    const int active = m_gateway.queryActiveSlot();
    if (active < 0) return;

    m_model.update([active](AppSnapshot& state) { state.activeSlot = active; });
}

void ProfileController::refreshState(const bool queryHardware) {
    const DeviceInfo device = m_gateway.describeDevice();
    const auto slots        = m_gateway.describeSlots();
    const int  activeSlot   = (queryHardware && device.connected) ? m_gateway.queryActiveSlot() : -1;

    m_model.update([&](AppSnapshot& state) {
        state.device = device;
        state.slots  = slots;
        if (!device.connected) {
            state.activeSlot = -1;
        } else if (queryHardware) {
            state.activeSlot = activeSlot;
        }
    });
}

void ProfileController::beginBusy(const std::string& text) {
    m_model.update([](AppSnapshot& state) { state.busy = true; });
    m_model.setStatus(StatusSeverity::Info, text);
}

void ProfileController::endBusy(const StatusSeverity severity, const std::string& text) {
    // Final message first, then drop the busy flag: the UI never shows a stale "working..." line.
    m_model.setStatus(severity, text);
    m_model.update([](AppSnapshot& state) { state.busy = false; });
}

// ---------------------------------------------------------------------------
// Commands (IProfileCommands)
// ---------------------------------------------------------------------------

void ProfileController::runBusy(const std::string& busyText, const std::function<void()>& body) {
    beginBusy(busyText);
    try {
        body();
    } catch (const std::exception& e) {
        endBusy(StatusSeverity::Error, std::string("Unexpected error: ") + e.what());
    } catch (...) {
        endBusy(StatusSeverity::Error, "Unexpected error.");
    }
}

void ProfileController::runCommand(const std::string& busyText, const std::function<CommandResult()>& command) {
    runBusy(busyText, [&] {
        const CommandResult result = guarded(command);
        guardedVoid([this] { refreshState(true); });
        endBusy(result.ok ? StatusSeverity::Success : StatusSeverity::Error, result.message);
    });
}

void ProfileController::activateSlot(const std::uint8_t slot) {
    const std::uint8_t target = clampSlot(slot);
    post([this, target] {
        runCommand("Switching to " + slotLabel(target) + "...", [this, target] { return m_gateway.activateSlot(target); });
    });
}

void ProfileController::cycleProfile(const bool forward) {
    post([this, forward] {
        runCommand(forward ? "Switching to the next profile..." : "Switching to the previous profile...",
                   [this, forward] { return m_gateway.cycleProfile(forward); });
    });
}

void ProfileController::applyActuation(const std::uint8_t slot, const ActuationSettings& settings, const bool saveToFlash) {
    const std::uint8_t target = clampSlot(slot);
    const ActuationSettings clean = actuation::sanitized(settings);
    post([this, target, clean, saveToFlash] {
        runBusy("Applying actuation to " + slotLabel(target) + "...",
                [&] { runApplyActuation(target, clean, saveToFlash); });
    });
}

void ProfileController::readActuation(const std::uint8_t slot) {
    const std::uint8_t target = clampSlot(slot);
    post([this, target] {
        runBusy("Reading " + slotLabel(target) + " from the keyboard...", [&] { runReadActuation(target); });
    });
}

void ProfileController::flashSlot(const std::uint8_t slot) {
    const std::uint8_t target = clampSlot(slot);
    post([this, target] {
        runBusy("Writing the profile to " + slotLabel(target) + "...", [&] { runFlashSlot(target); });
    });
}

void ProfileController::refresh() {
    post([this] { refreshState(true); });
}

void ProfileController::runApplyActuation(const std::uint8_t slot, const ActuationSettings& settings, const bool saveToFlash) {
    const CommandResult applied = guarded([&] { return m_gateway.applyActuation(slot, settings); });
    if (!applied.ok) {
        guardedVoid([this] { refreshState(true); });
        endBusy(StatusSeverity::Error, applied.message);
        return;
    }

    StatusSeverity severity = StatusSeverity::Success;
    std::string    message;

    // Trust, but verify: ask the keyboard what it actually stored.
    const auto effective = guardedRead([&] { return m_gateway.readActuation(slot, settings); });
    if (effective) {
        m_model.update([&](AppSnapshot& state) { state.actuation[slot] = *effective; });
        if (*effective == settings) {
            message = slotLabel(slot) + ": " + actuation::describe(settings) + " (verified on the keyboard).";
        } else {
            severity = StatusSeverity::Warning;
            message  = "The keyboard accepted the command but reports " + actuation::describe(*effective) +
                       " instead of " + actuation::describe(settings) + ".";
            if (effective->rapidTrigger != settings.rapidTrigger) {
                message += " The Rapid Trigger mode byte may not match the firmware (see README, Firmware notes).";
            }
        }
    } else {
        m_model.update([&](AppSnapshot& state) { state.actuation[slot] = settings; });
        severity = StatusSeverity::Info;
        message  = slotLabel(slot) + ": " + actuation::describe(settings) + " applied (could not read back to verify).";
    }

    if (saveToFlash) {
        const CommandResult saved = guarded([&] { return m_gateway.saveSlot(slot); });
        if (saved.ok) {
            message += " Saved to the keyboard's flash.";
        } else {
            if (severity == StatusSeverity::Success) severity = StatusSeverity::Warning;
            message += " Saving to flash failed: " + saved.message;
        }
    }

    guardedVoid([this] { refreshState(true); });
    endBusy(severity, message);
}

void ProfileController::runReadActuation(const std::uint8_t slot) {
    const auto effective = guardedRead([&] { return m_gateway.readActuation(slot, std::nullopt); });
    if (effective) {
        m_model.update([&](AppSnapshot& state) { state.actuation[slot] = *effective; });
        endBusy(StatusSeverity::Info, slotLabel(slot) + " on the keyboard: " + actuation::describe(*effective) + ".");
    } else {
        endBusy(StatusSeverity::Warning, "Could not read " + slotLabel(slot) + " from the keyboard.");
    }
}

void ProfileController::runFlashSlot(const std::uint8_t slot) {
    const CommandResult flashed = guarded([&] { return m_gateway.flashSlotProfile(slot); });
    if (!flashed.ok) {
        guardedVoid([this] { refreshState(true); });
        endBusy(StatusSeverity::Error, flashed.message);
        return;
    }

    StatusSeverity severity = StatusSeverity::Success;
    std::string    message  = "Profile sent to " + slotLabel(slot) + ".";

    // Compare what the keyboard now holds with what the profile file says it should hold.
    const auto expected  = m_gateway.describeSlots()[slot].storedSettings;
    const auto effective = guardedRead([&] { return m_gateway.readActuation(slot, expected); });
    if (effective) {
        m_model.update([&](AppSnapshot& state) { state.actuation[slot] = *effective; });
        if (expected && *effective != *expected) {
            severity = StatusSeverity::Warning;
            message  = "Profile sent to " + slotLabel(slot) + ", but the keyboard reports " + actuation::describe(*effective) +
                       " while the profile file says " + actuation::describe(*expected) +
                       ". The keyboard may not accept raw profile writes (see README, Firmware notes).";
        } else if (expected) {
            message += " Verified: the keyboard reports the profile's actuation settings.";
        }
    } else {
        severity = StatusSeverity::Info;
        message += " (Could not read back to verify.)";
    }

    guardedVoid([this] { refreshState(true); });
    endBusy(severity, message);
}

// ---------------------------------------------------------------------------
// Watcher events
// ---------------------------------------------------------------------------

void ProfileController::onFileCreated(const std::filesystem::path& path) {
    post([this, path] {
        guardedVoid([&] { m_gateway.handleFileCreated(path); });
        guardedVoid([this] { refreshState(false); });
        m_model.setStatus(StatusSeverity::Info, "Profile file added: " + path.filename().string());
    });
}

void ProfileController::onFileDeleted(const std::filesystem::path& path) {
    post([this, path] {
        guardedVoid([&] { m_gateway.handleFileDeleted(path); });
        guardedVoid([this] { refreshState(false); });
        m_model.setStatus(StatusSeverity::Info, "Profile file removed: " + path.filename().string());
    });
}

void ProfileController::onDeviceConnected(const kbd::TransportMode mode, const std::string& path) {
    post([this, mode, path] {
        guardedVoid([&] { m_gateway.handleDeviceConnected(mode, path); }); // re-applies the active profile
        guardedVoid([this] { refreshState(true); });
        m_model.update([&](AppSnapshot& state) { state.device = DeviceInfo{true, mode, path}; });
        m_model.setStatus(StatusSeverity::Success, std::string("Keyboard connected (") + transportName(mode) + ").");
    });
}

void ProfileController::onDeviceDisconnected() {
    post([this] {
        guardedVoid([this] { m_gateway.handleDeviceDisconnected(); });
        guardedVoid([this] { refreshState(false); });
        m_model.update([](AppSnapshot& state) {
            state.device     = DeviceInfo{};
            state.activeSlot = -1;
        });
        m_model.setStatus(StatusSeverity::Warning, "Keyboard disconnected.");
    });
}

} // namespace ui
