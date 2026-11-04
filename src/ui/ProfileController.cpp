#include "ui/ProfileController.h"
#include "files/Logger.h"
#include "ui/KeyboardLayout.h"

#include <algorithm>
#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

    std::optional<KeyMatrix> guardedReadMatrix(const std::function<std::optional<KeyMatrix>()>& read) {
        try {
            return read();
        } catch (const std::exception& e) {
            LOG_WARN("UI", std::string("Reading the key matrix failed: ") + e.what());
        } catch (...) {
            LOG_WARN("UI", "Reading the key matrix failed.");
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
            // Live key travel needs ~60 readings a second; everything else is polled every m_pollInterval.
            const bool live = m_polling.load() && m_liveWanted.load();
            const auto timeout = live ? std::min<std::chrono::milliseconds>(LIVE_POLL_INTERVAL, m_pollInterval) : m_pollInterval;
            const bool woken = m_wakeup.wait_for(lock, stopToken, timeout, [this] { return !m_tasks.empty() || m_reschedule.load(); });
            if (stopToken.stop_requested()) break;
            if (m_reschedule.exchange(false) && m_tasks.empty()) continue; // the poll rate changed: wait again with it
            const bool hasWork = woken && !m_tasks.empty();

            if (!hasWork) { // timed out with nothing queued
                if (m_polling.load()) {
                    lock.unlock();
                    if (live) guardedVoid([this] { pollLiveTravel(); });
                    const auto now = std::chrono::steady_clock::now();
                    if (!live || now - m_lastSlotPoll >= m_pollInterval) {
                        m_lastSlotPoll = now;
                        guardedVoid([this] { pollActiveSlot(); });
                    }
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
    if (m_lastSyncedSlot < 0) {
        m_lastSyncedSlot = active; // first look after start-up / reconnect: nothing was switched, change nothing
    } else if (active != m_lastSyncedSlot) {
        syncControllerMode(active); // switched on the keyboard (Fn + P) or by another program
    }
    // While the stored and the running mode disagree, watch the device list: a replug or a USB restart from
    // the PC clears it (a replug also reconnects, which refreshes everything).
    if (m_model.snapshot().controller.replugNeeded) {
        const auto enumerated = m_gateway.enumeratedXInput();
        m_model.update([enumerated](AppSnapshot& state) { state.controller = state.controller.withEnumeration(enumerated); });
    }
}

void ProfileController::syncControllerMode(const int activeSlot) {
    if (activeSlot < 0 || activeSlot >= static_cast<int>(SLOT_COUNT)) return;
    m_lastSyncedSlot = activeSlot;
    if (!m_autoControllerMode || !m_gateway.describeDevice().connected) return;

    const auto slot    = static_cast<std::uint8_t>(activeSlot);
    const auto details = m_gateway.readSlotDetails(slot);
    if (!details) return;
    const bool hasPadKeys = std::any_of(details->keys.begin(), details->keys.end(),
                                        [](const KeyAdvanced& key) { return key.mode == kbd::AdvancedMode::GAMEPAD; });
    const auto settings = m_gateway.readControllerSettings();
    const auto enumerated = m_gateway.enumeratedXInput();
    m_model.update([&](AppSnapshot& state) {
        state.details[slot] = *details;
        if (settings) state.controller = settings->withEnumeration(enumerated);
    });
    if (!settings || !settings->mode) return;

    const std::uint8_t mode = *settings->mode;
    const std::uint8_t want = hasPadKeys ? static_cast<std::uint8_t>(mode | kbd::ControllerMode::XINPUT)
                                         : static_cast<std::uint8_t>(mode & ~kbd::ControllerMode::XINPUT);
    if (want == mode) return;

    const CommandResult applied = m_gateway.applyControllerMode(want);
    if (!applied.ok) {
        m_model.setStatus(StatusSeverity::Warning, "Automatic gamepad mode: " + applied.message);
        return;
    }
    m_model.update([want, enumerated](AppSnapshot& state) {
        state.controller.mode = want;
        state.controller = state.controller.withEnumeration(enumerated);
    });
    // Added to the line the switch itself reported, so "Switched to slot 2" stays visible and keeps its colour.
    const std::string note = slotLabel(slot) + (hasPadKeys ? " has gamepad keys, so XInput mode was turned on."
                                                           : " has no gamepad keys, so XInput mode was turned off.") +
                             " Unplug the cable and plug it back in to finish.";
    const StatusMessage previous = m_model.snapshot().status;
    if (previous.severity == StatusSeverity::Success && !previous.text.empty()) {
        m_model.setStatus(StatusSeverity::Success, previous.text + " " + note);
    } else {
        m_model.setStatus(StatusSeverity::Info, note);
    }
}

void ProfileController::setAutoControllerMode(const bool enabled) {
    m_autoControllerMode = enabled;
    if (enabled) {
        post([this] {
            const int active = m_model.snapshot().activeSlot;
            guardedVoid([&] { syncControllerMode(active); });
        });
    }
}

bool ProfileController::autoControllerMode() const noexcept {
    return m_autoControllerMode;
}

void ProfileController::pollLiveTravel() {
    std::vector<KeyPosition> keys;
    {
        std::lock_guard<std::mutex> lock(m_liveMutex);
        keys = m_liveKeys;
    }
    if (keys.empty() || !m_gateway.describeDevice().connected) return;

    std::vector<LiveKeyTravel> readings;
    readings.reserve(keys.size());
    for (const KeyPosition& key : keys) {
        if (const auto reading = m_gateway.readLiveTravel(key)) readings.push_back(*reading);
    }
    std::lock_guard<std::mutex> lock(m_liveMutex);
    if (m_liveKeys == keys) m_liveReadings = std::move(readings); // the watch list may have changed meanwhile
}

void ProfileController::refreshState(const bool queryHardware) {
    const DeviceInfo device  = m_gateway.describeDevice();
    const auto       slots   = m_gateway.describeSlots();
    const auto       library = m_gateway.describeLibrary();
    const int        active  = (queryHardware && device.connected) ? m_gateway.queryActiveSlot() : -1;

    m_model.update([&](AppSnapshot& state) {
        state.device  = device;
        state.slots   = slots;
        state.library = library;
        if (!device.connected) {
            state.activeSlot = -1;
        } else if (queryHardware) {
            state.activeSlot = active;
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
        const int active = m_model.snapshot().activeSlot;
        guardedVoid([&] { syncControllerMode(active); });
    });
}

void ProfileController::cycleProfile(const bool forward) {
    post([this, forward] {
        runCommand(forward ? "Switching to the next profile..." : "Switching to the previous profile...",
                   [this, forward] { return m_gateway.cycleProfile(forward); });
        const int active = m_model.snapshot().activeSlot;
        guardedVoid([&] { syncControllerMode(active); });
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

void ProfileController::applyKeyActuation(const std::uint8_t slot, std::vector<KeyPosition> keys, const KeyActuation& value,
                                          const bool saveToFlash) {
    const std::uint8_t  target = clampSlot(slot);
    const KeyActuation  clean  = actuation::sanitized(value);
    post([this, target, keys = std::move(keys), clean, saveToFlash] {
        runBusy("Applying to " + std::to_string(keys.size()) + (keys.size() == 1 ? " key" : " keys") + " of " +
                    slotLabel(target) + "...",
                [&] { runApplyKeyActuation(target, keys, clean, saveToFlash); });
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
        }
    } else {
        m_model.update([&](AppSnapshot& state) { state.actuation[slot] = settings; });
        severity = StatusSeverity::Info;
        message  = slotLabel(slot) + ": " + actuation::describe(settings) + " applied (could not read back to verify).";
    }

    saveIfRequested(slot, saveToFlash, severity, message);
    guardedVoid([this] { refreshState(true); });
    endBusy(severity, message);
}

void ProfileController::runApplyKeyActuation(const std::uint8_t slot, const std::vector<KeyPosition>& keys,
                                             const KeyActuation& value, const bool saveToFlash) {
    const CommandResult applied = guarded([&] { return m_gateway.applyKeyActuation(slot, keys, value); });
    if (!applied.ok) {
        guardedVoid([this] { refreshState(true); });
        endBusy(StatusSeverity::Error, applied.message);
        return;
    }

    StatusSeverity severity = StatusSeverity::Success;
    std::string    message  = applied.message;

    const auto readBack = guardedReadMatrix([&] { return m_gateway.readKeyMatrix(slot); });
    if (readBack) {
        const std::size_t wrong = static_cast<std::size_t>(std::count_if(keys.begin(), keys.end(), [&](const KeyPosition& key) {
            return readBack->at(key) != value;
        }));
        m_model.update([&](AppSnapshot& state) { state.keyMatrix[slot] = *readBack; });
        if (wrong == 0) {
            message += " Verified on the keyboard.";
        } else {
            severity = StatusSeverity::Warning;
            message  = "The keyboard accepted the command, but " + std::to_string(wrong) + " of " +
                       std::to_string(keys.size()) + " keys report different values.";
        }
    } else {
        // No read-back (dry run): show what was sent on top of what we knew.
        m_model.update([&](AppSnapshot& state) {
            KeyMatrix matrix = state.keyMatrix[slot].value_or(state.slots[slot].storedKeys.value_or(KeyMatrix{}));
            for (const KeyPosition& key : keys) matrix.at(key) = value;
            state.keyMatrix[slot] = matrix;
        });
        severity = StatusSeverity::Info;
        message += " (Could not read back to verify.)";
    }

    saveIfRequested(slot, saveToFlash, severity, message);
    guardedVoid([this] { refreshState(true); });
    endBusy(severity, message);
}

void ProfileController::saveIfRequested(const std::uint8_t slot, const bool saveToFlash, StatusSeverity& severity,
                                        std::string& message) {
    if (!saveToFlash) return;
    const CommandResult saved = guarded([&] { return m_gateway.saveSlot(slot); });
    if (saved.ok) {
        message += " Saved to the keyboard's flash.";
    } else {
        if (severity == StatusSeverity::Success || severity == StatusSeverity::Info) severity = StatusSeverity::Warning;
        message += " Saving to flash failed: " + saved.message;
    }
}

void ProfileController::runReadActuation(const std::uint8_t slot) {
    const auto effective = guardedRead([&] { return m_gateway.readActuation(slot, std::nullopt); });
    const auto keys      = guardedReadMatrix([&] { return m_gateway.readKeyMatrix(slot); });

    m_model.update([&](AppSnapshot& state) {
        if (effective) state.actuation[slot] = *effective;
        if (keys) state.keyMatrix[slot] = *keys;
    });

    if (!effective) {
        endBusy(StatusSeverity::Warning, "Could not read " + slotLabel(slot) + " from the keyboard.");
        return;
    }
    std::string message = slotLabel(slot) + " on the keyboard: " + actuation::describe(*effective);
    if (keys) {
        const std::size_t overrides = keys->overrideCount();
        message += overrides == 0 ? ", every key follows it." : ", " + std::to_string(overrides) + " keys with their own settings.";
    } else {
        message += " (per-key settings could not be read).";
    }
    endBusy(StatusSeverity::Info, message);
}

void ProfileController::runFlashSlot(const std::uint8_t slot) {
    const CommandResult written = guarded([&] { return m_gateway.flashSlotProfile(slot); });
    if (!written.ok) {
        guardedVoid([this] { refreshState(true); });
        endBusy(StatusSeverity::Error, written.message);
        return;
    }

    StatusSeverity severity = StatusSeverity::Success;
    std::string    message  = written.message;

    // Compare what the keyboard now holds with what the profile file says it should hold.
    const SlotInfo expected = m_gateway.describeSlots()[slot];
    const auto global = guardedRead([&] { return m_gateway.readActuation(slot, expected.storedSettings); });
    const auto keys   = guardedReadMatrix([&] { return m_gateway.readKeyMatrix(slot); });

    m_model.update([&](AppSnapshot& state) {
        if (global) state.actuation[slot] = *global;
        if (keys) state.keyMatrix[slot] = *keys;
    });

    if (!global || !keys) {
        severity = StatusSeverity::Info;
        message += " (Could not read back to verify.)";
    } else {
        std::vector<std::string> problems;
        if (expected.storedSettings && *global != *expected.storedSettings) {
            problems.push_back("the slot reports " + actuation::describe(*global) + " while the file says " +
                               actuation::describe(*expected.storedSettings));
        }
        if (expected.storedKeys) {
            std::size_t wrong = 0;
            for (std::size_t i = 0; i < keys->keys.size(); ++i) {
                if (keys->keys[i] != actuation::sanitized(expected.storedKeys->keys[i])) ++wrong;
            }
            if (wrong > 0) problems.push_back(std::to_string(wrong) + " keys differ from the file");
        }
        if (problems.empty()) {
            message += " Verified on the keyboard.";
        } else {
            severity = StatusSeverity::Warning;
            message += " But after writing, ";
            for (std::size_t i = 0; i < problems.size(); ++i) message += (i > 0 ? " and " : "") + problems[i];
            message += ".";
        }
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
        m_model.update([&](AppSnapshot& state) {
            state.device.connected = true;
            state.device.transport = mode;
            state.device.path      = path;
        });
        // A (re)connect is a fresh enumeration: compare what the keyboard stores with what the PC now runs.
        // The first slot seen afterwards is only remembered, so plugging in never changes the mode.
        m_lastSyncedSlot = -1;
        std::optional<ControllerSettings> settings;
        guardedVoid([&] { settings = m_gateway.readControllerSettings(); });
        const auto enumerated = m_gateway.enumeratedXInput();
        m_model.update([&](AppSnapshot& state) {
            state.controller = settings ? settings->withEnumeration(enumerated) : ControllerSettings{};
        });
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

// ---------------------------------------------------------------------------
// Profile library
// ---------------------------------------------------------------------------

void ProfileController::reorderLibrary(const std::size_t fromIndex, const std::size_t toIndex) {
    post([this, fromIndex, toIndex] {
        const CommandResult moved = guarded([&] { return m_gateway.reorderLibrary(fromIndex, toIndex); });
        guardedVoid([this] { refreshState(false); });
        if (!moved.ok) m_model.setStatus(StatusSeverity::Warning, moved.message);
    });
}

void ProfileController::assignProfileToSlot(const std::string& fileName, const std::uint8_t slot, const bool writeNow) {
    const std::uint8_t target = clampSlot(slot);
    post([this, fileName, target, writeNow] {
        if (!writeNow) {
            runCommand("Assigning " + fileName + " to " + slotLabel(target) + "...",
                       [&] { return m_gateway.assignProfileToSlot(fileName, target, false); });
            return;
        }
        // Writing reuses the flash path so the result is read back and verified like "Write to keyboard".
        runBusy("Writing " + fileName + " to " + slotLabel(target) + "...", [&] {
            const CommandResult assigned = guarded([&] { return m_gateway.assignProfileToSlot(fileName, target, false); });
            if (!assigned.ok) {
                guardedVoid([this] { refreshState(true); });
                endBusy(StatusSeverity::Error, assigned.message);
                return;
            }
            runFlashSlot(target);
        });
    });
}

void ProfileController::clearSlot(const std::uint8_t slot) {
    const std::uint8_t target = clampSlot(slot);
    post([this, target] {
        runCommand("Clearing " + slotLabel(target) + "...", [this, target] { return m_gateway.clearSlot(target); });
    });
}

// ---------------------------------------------------------------------------
// Advanced keys, SOCD, game controller
// ---------------------------------------------------------------------------

namespace {

    std::string countText(const std::size_t count, const char* singular, const char* plural) {
        return std::to_string(count) + " " + (count == 1 ? singular : plural);
    }

    std::string describeDetails(const SlotDetails& details) {
        std::size_t gamepad = 0, dynamic = 0, toggle = 0;
        for (std::uint8_t row = 0; row < MATRIX_ROWS; ++row) {
            for (std::uint8_t col = 0; col < MATRIX_COLS; ++col) {
                if (!isKeyOnBoard({row, col})) continue;
                switch (details.at({row, col}).mode) {
                    case kbd::AdvancedMode::GAMEPAD: ++gamepad; break;
                    case kbd::AdvancedMode::DKS:     ++dynamic; break;
                    case kbd::AdvancedMode::TOGGLE:  ++toggle; break;
                    default: break;
                }
            }
        }
        return countText(gamepad, "gamepad key", "gamepad keys") + ", " + countText(dynamic, "dynamic keystroke", "dynamic keystrokes") +
               ", " + countText(toggle, "toggle key", "toggle keys") + ", " + countText(details.socdPairs().size(), "SOCD pair", "SOCD pairs");
    }

    // What a key's stored mode should be after `change`.
    KeyAdvanced expectedAfter(const AdvancedKeyChange& change) {
        switch (change.kind) {
            case AdvancedKeyChange::Kind::Gamepad:          return {kbd::AdvancedMode::GAMEPAD, change.index};
            case AdvancedKeyChange::Kind::Toggle:           return {kbd::AdvancedMode::TOGGLE, 0};
            case AdvancedKeyChange::Kind::DynamicKeystroke: return {kbd::AdvancedMode::DKS, change.index};
            case AdvancedKeyChange::Kind::Clear:            break;
        }
        return {};
    }

    std::string describeMode(const std::uint8_t mode) {
        std::string text = (mode & kbd::ControllerMode::XINPUT) != 0 ? "XInput (Xbox-style)" : "generic HID joystick";
        text += (mode & kbd::ControllerMode::TYPING) != 0 ? ", gamepad keys also type" : ", gamepad keys only drive the controller";
        return text;
    }

} // namespace

void ProfileController::storeDetails(const std::uint8_t slot) {
    std::optional<SlotDetails> details;
    guardedVoid([&] { details = m_gateway.readSlotDetails(slot); });
    if (details) m_model.update([&](AppSnapshot& state) { state.details[slot] = *details; });
}

void ProfileController::readSlotDetails(const std::uint8_t slot) {
    const std::uint8_t target = clampSlot(slot);
    post([this, target] {
        runBusy("Reading the keys of " + slotLabel(target) + "...", [&] {
            std::optional<SlotDetails> details;
            guardedVoid([&] { details = m_gateway.readSlotDetails(target); });
            if (!details) {
                endBusy(StatusSeverity::Warning, "Could not read the keys of " + slotLabel(target) + " from the keyboard.");
                return;
            }
            m_model.update([&](AppSnapshot& state) { state.details[target] = *details; });
            endBusy(StatusSeverity::Info, slotLabel(target) + ": " + describeDetails(*details) + ".");
        });
    });
}

void ProfileController::applyAdvancedKeys(const std::uint8_t slot, std::vector<AdvancedKeyChange> changes, const bool saveToFlash) {
    const std::uint8_t target = clampSlot(slot);
    if (changes.empty()) return;
    post([this, target, changes = std::move(changes), saveToFlash] {
        runBusy("Updating " + countText(changes.size(), "key", "keys") + " of " + slotLabel(target) + "...",
                [&] { runApplyAdvancedKeys(target, changes, saveToFlash); });
    });
}

void ProfileController::runApplyAdvancedKeys(const std::uint8_t slot, const std::vector<AdvancedKeyChange>& changes,
                                             const bool saveToFlash) {
    for (const AdvancedKeyChange& change : changes) {
        const CommandResult applied = guarded([&] { return m_gateway.applyAdvancedKey(slot, change); });
        if (!applied.ok) {
            storeDetails(slot);
            endBusy(StatusSeverity::Error, applied.message);
            return;
        }
    }

    StatusSeverity severity = StatusSeverity::Success;
    std::string    message  = countText(changes.size(), "key", "keys") + " of " + slotLabel(slot) + " updated.";

    std::optional<SlotDetails> readBack;
    guardedVoid([&] { readBack = m_gateway.readSlotDetails(slot); });
    if (readBack) {
        const std::size_t wrong = static_cast<std::size_t>(std::count_if(changes.begin(), changes.end(), [&](const AdvancedKeyChange& change) {
            return readBack->at(change.key) != expectedAfter(change);
        }));
        m_model.update([&](AppSnapshot& state) { state.details[slot] = *readBack; });
        if (wrong == 0) {
            message += " Verified on the keyboard.";
        } else {
            severity = StatusSeverity::Warning;
            message  = "The keyboard accepted the change, but " + std::to_string(wrong) + " of " + std::to_string(changes.size()) +
                       " keys report a different mode.";
        }
    } else {
        m_model.update([&](AppSnapshot& state) {
            SlotDetails details = state.details[slot].value_or(SlotDetails{});
            for (const AdvancedKeyChange& change : changes) {
                details.set(change.key, expectedAfter(change));
                if (change.kind == AdvancedKeyChange::Kind::DynamicKeystroke && change.index < details.okmc.size()) {
                    details.okmc[change.index] = change.okmc;
                }
            }
            state.details[slot] = details;
        });
        severity = StatusSeverity::Info;
        message += " (Could not read back to verify.)";
    }

    saveIfRequested(slot, saveToFlash, severity, message);
    guardedVoid([this] { refreshState(true); });
    endBusy(severity, message);
}

void ProfileController::applySocdPairs(const std::uint8_t slot, std::vector<kbd::SocdPair> pairs, const bool saveToFlash) {
    const std::uint8_t target = clampSlot(slot);
    post([this, target, pairs = std::move(pairs), saveToFlash] {
        runBusy("Writing the SOCD pairs of " + slotLabel(target) + "...", [&] { runApplySocd(target, pairs, saveToFlash); });
    });
}

void ProfileController::runApplySocd(const std::uint8_t slot, const std::vector<kbd::SocdPair>& requested, const bool saveToFlash) {
    std::vector<kbd::SocdPair> pairs;
    for (const kbd::SocdPair& pair : requested) {
        const bool sameKey = pair.row1 == pair.row2 && pair.col1 == pair.col2;
        if (pair.isSet() && pair.type <= kbd::SocdPair::TYPE_MAX && !sameKey) pairs.push_back(pair);
    }
    if (pairs.size() > kbd::ProfileMemory::SOCD_COUNT) pairs.resize(kbd::ProfileMemory::SOCD_COUNT);

    for (std::size_t i = 0; i < kbd::ProfileMemory::SOCD_COUNT; ++i) {
        const kbd::SocdPair entry = i < pairs.size() ? pairs[i] : kbd::SocdPair{};
        const CommandResult applied = guarded([&] { return m_gateway.applySocd(slot, i, entry); });
        if (!applied.ok) {
            storeDetails(slot);
            endBusy(StatusSeverity::Error, applied.message);
            return;
        }
    }

    StatusSeverity severity = StatusSeverity::Success;
    std::string    message  = slotLabel(slot) + ": " + countText(pairs.size(), "SOCD pair", "SOCD pairs") + " written.";

    std::optional<SlotDetails> readBack;
    guardedVoid([&] { readBack = m_gateway.readSlotDetails(slot); });
    if (readBack) {
        m_model.update([&](AppSnapshot& state) { state.details[slot] = *readBack; });
        if (readBack->socdPairs() == pairs) {
            message += " Verified on the keyboard.";
        } else {
            severity = StatusSeverity::Warning;
            message  = "The keyboard accepted the SOCD pairs, but reports " + countText(readBack->socdPairs().size(), "pair", "pairs") +
                       " that differ from what was sent.";
        }
    } else {
        m_model.update([&](AppSnapshot& state) {
            SlotDetails details = state.details[slot].value_or(SlotDetails{});
            for (std::size_t i = 0; i < details.socd.size(); ++i) details.socd[i] = i < pairs.size() ? pairs[i] : kbd::SocdPair{};
            state.details[slot] = details;
        });
        severity = StatusSeverity::Info;
        message += " (Could not read back to verify.)";
    }

    saveIfRequested(slot, saveToFlash, severity, message);
    endBusy(severity, message);
}

void ProfileController::readControllerSettings() {
    post([this] {
        std::optional<ControllerSettings> settings;
        guardedVoid([&] { settings = m_gateway.readControllerSettings(); });
        if (!settings) {
            if (m_gateway.describeDevice().connected) {
                m_model.setStatus(StatusSeverity::Warning, "Could not read the controller settings from the keyboard.");
            }
            return;
        }
        const auto enumerated = m_gateway.enumeratedXInput();
        m_model.update([&](AppSnapshot& state) { state.controller = settings->withEnumeration(enumerated); });
    });
}

void ProfileController::applyControllerMode(const std::uint8_t mode) {
    const std::uint8_t clean = mode & kbd::ControllerMode::KNOWN_BITS;
    post([this, clean] {
        runBusy("Saving the controller mode...", [&] {
            const CommandResult applied = guarded([&] { return m_gateway.applyControllerMode(clean); });
            if (!applied.ok) {
                endBusy(StatusSeverity::Error, applied.message);
                return;
            }
            std::optional<ControllerSettings> settings;
            guardedVoid([&] { settings = m_gateway.readControllerSettings(); });
            const auto enumerated = m_gateway.enumeratedXInput();
            bool replug = false;
            m_model.update([&](AppSnapshot& state) {
                ControllerSettings next = settings ? *settings : state.controller;
                if (!settings) next.mode = clean;
                state.controller = next.withEnumeration(enumerated);
                replug = state.controller.replugNeeded;
            });
            if (settings && settings->mode && *settings->mode != clean) {
                endBusy(StatusSeverity::Warning, "The keyboard reports a different controller mode: " + describeMode(*settings->mode) + ".");
                return;
            }
            endBusy(StatusSeverity::Success, "Controller mode saved: " + describeMode(clean) +
                                                 (replug ? ". It starts the next time the keyboard is enumerated (replug the cable)."
                                                         : ". The keyboard is already running it."));
        });
    });
}

void ProfileController::applyCurve(const kbd::ResponseCurve& curve) {
    post([this, curve] {
        runBusy("Saving the response curve...", [&] {
            if (!kbd::isValidCurve(curve)) {
                endBusy(StatusSeverity::Error, "The curve's points must stay in order (0 - 4.0 mm, 0 - 127).");
                return;
            }
            const CommandResult applied = guarded([&] { return m_gateway.applyCurve(curve); });
            if (!applied.ok) {
                endBusy(StatusSeverity::Error, applied.message);
                return;
            }
            std::optional<ControllerSettings> settings;
            guardedVoid([&] { settings = m_gateway.readControllerSettings(); });
            const auto enumerated = m_gateway.enumeratedXInput();
            m_model.update([&](AppSnapshot& state) {
                if (settings) {
                    state.controller = settings->withEnumeration(enumerated);
                } else {
                    state.controller.curve = curve;
                }
            });
            if (settings && settings->curve && *settings->curve != curve) {
                endBusy(StatusSeverity::Warning, "The keyboard accepted the curve but reports different points.");
                return;
            }
            endBusy(StatusSeverity::Success, std::string("Response curve saved") + (settings ? " and verified." : "."));
        });
    });
}

void ProfileController::watchLiveTravel(std::vector<KeyPosition> keys) {
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    {
        std::lock_guard<std::mutex> lock(m_liveMutex);
        if (keys == m_liveKeys) return;
        m_liveKeys = std::move(keys);
        if (m_liveKeys.empty()) m_liveReadings.clear();
        m_liveWanted.store(!m_liveKeys.empty());
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex); // so the worker cannot miss the flag between its check and its wait
        m_reschedule.store(true);
    }
    m_wakeup.notify_one(); // switch to the faster (or slower) poll at once
}

std::vector<LiveKeyTravel> ProfileController::liveTravel() const {
    std::lock_guard<std::mutex> lock(m_liveMutex);
    return m_liveReadings;
}

} // namespace ui
