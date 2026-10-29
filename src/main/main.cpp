#include "kbd/KeyboardDevice.h"
#include "kbd/DeviceWatcher.h"
#include "kbd/KbdException.h"
#include "files/ProfileService.h"
#include "files/DirectoryWatcher.h"
#include "files/IniConfig.h"
#include "files/AppPaths.h"
#include "files/Logger.h"
#include "ui/UIManager.h"
#include "ui/QuickProfilePickerView.h"

#include <iostream>
#include <condition_variable>
#include <mutex>
#include <csignal>

#if defined(_WIN32)
    #include "kbd/HotkeyManager.h"
    #include "ui/TrayManager.h"
#else
    #include "kbd/IpcServer.h"
#endif

// Global condition variable to hold daemon execution until OS shutdown signal
std::condition_variable g_shutdownCv;
std::mutex              g_shutdownMutex;
bool                    g_shouldTerminate = false;

static void triggerShutdown() {
    {
        std::lock_guard<std::mutex> lock(g_shutdownMutex);
        g_shouldTerminate = true;
    }
    g_shutdownCv.notify_all();
}

#if defined(_WIN32)
static BOOL WINAPI win32ConsoleHandler(DWORD signal) {
    switch (signal) {
        case CTRL_C_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            LOG_INFO("Daemon", "Received Windows termination event. Stopping...");
            triggerShutdown();
            return TRUE;
        default:
            return FALSE;
    }
}
#else
void posixSignalHandler(int sig) {
    LOG_INFO("Daemon", "Received POSIX signal (" + std::to_string(sig) + "). Stopping...");
    triggerShutdown();
}
#endif

int main(int argc, char* argv[]) {
    // 1. Determine default debug mode based on CMake build type
#if defined(KBD_DEBUG_BUILD) || defined(_DEBUG) || !defined(NDEBUG)
    bool debugMode = true;  // Debug build: Console visible & live terminal logs ON by default
#else
    bool debugMode = false; // Release build: Silent background daemon, file logs only
#endif

    // 2. Command-line argument overrides
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--debug") {
            debugMode = true;
        } else if (arg == "--silent" || arg == "--headless") {
            debugMode = false;
        }
#if !defined(_WIN32)
        else if (arg == "--next" || arg == "--prev" || arg == "--slot1" || arg == "--slot2" || arg == "--slot3") {
            if (kbd::IpcServer::sendCommand(arg)) return 0;
            std::cerr << "[Error] Daemon not running on /tmp/kbd_daemon.sock\n";
            return 1;
        }
#endif
    }

#if defined(_WIN32)
    // 3. In Release (or if not in debugMode), detach from the console window
    if (!debugMode) {
        FreeConsole();
    }
    SetConsoleCtrlHandler(win32ConsoleHandler, TRUE);
#else
    std::signal(SIGINT, posixSignalHandler);
    std::signal(SIGTERM, posixSignalHandler);
#endif

    // 4. Initialize File Paths & Logger
    Files::AppPaths::ensureDirectoriesExist();
    Files::Logger::getInstance().init(Files::AppPaths::getLogFilePath(), debugMode);

    if (debugMode) {
        LOG_INFO("Daemon", "=== RUNNING IN DEBUG MODE (Console active, live terminal logging ON) ===");
    } else {
        LOG_INFO("Daemon", "=== RUNNING IN RELEASE MODE (Headless background service, logging to file) ===");
    }

    LOG_INFO("Paths", "Profiles Directory: " + Files::AppPaths::getProfilesDir().string());
    LOG_INFO("Paths", "Config File: " + Files::AppPaths::getConfigFilePath().string());

    try {
        // 5. Load & Validate config.ini
        Files::IniConfig config;
        config.load(Files::AppPaths::getConfigFilePath());

        std::string nextKeyStr = config.get("Hotkeys", "next_profile", "Ctrl+Shift+]");
        std::string prevKeyStr = config.get("Hotkeys", "prev_profile", "Ctrl+Shift+[");

        auto nextResult = Files::IniConfig::parseHotkey(nextKeyStr);
        if (!nextResult.isValid) {
            LOG_WARN("Config", "Invalid next_profile hotkey in config.ini. Defaulting to Ctrl+Shift+]");
            nextKeyStr = "Ctrl+Shift+]";
            nextResult = Files::IniConfig::parseHotkey(nextKeyStr);
        }

        auto prevResult = Files::IniConfig::parseHotkey(prevKeyStr);
        if (!prevResult.isValid) {
            LOG_WARN("Config", "Invalid prev_profile hotkey in config.ini. Defaulting to Ctrl+Shift+[");
            prevKeyStr = "Ctrl+Shift+[";
            prevResult = Files::IniConfig::parseHotkey(prevKeyStr);
        }

        // 6. Connect to Hardware (USB-C Wired Priority)
        kbd::KeyboardDevice keyboard;
        if (!keyboard.connect()) {
            LOG_WARN("Hardware", "Keychron K2 HE not found on startup. DeviceWatcher will probe continuously.");
        } else {
            LOG_INFO("Hardware", "Connected to Keychron K2 HE (" + keyboard.getDevicePath() + ")");
        }

        // 7. Profile Management & File Watcher
        kbd::ProfileService profileService(keyboard);
        profileService.loadAllFromDirectory(Files::AppPaths::getProfilesDir());

        Files::DirectoryWatcher watcher(Files::AppPaths::getProfilesDir());
        watcher.addObserver(&profileService);
        watcher.start();

        kbd::DeviceWatcher deviceWatcher(keyboard);
        deviceWatcher.addObserver(&profileService);
        deviceWatcher.start();

        // 8. Initialize Dear ImGui UI Engine
        auto uiManager = std::make_shared<UI::UIManager>();
        auto pickerView = std::make_shared<UI::QuickProfilePickerView>(profileService);
        uiManager->addView(pickerView);
        uiManager->start();

        // 9. Shortcut Engine & Tray Integration
#if defined(_WIN32)
        kbd::TrayManager tray(profileService, *uiManager, [] {
            triggerShutdown();
        });
        tray.start();

        kbd::HotkeyManager hotkeys(
            [&] {
                LOG_INFO("Hotkey", "Action: Next Profile");
                profileService.cycleForward();
            },
            [&] {
                LOG_INFO("Hotkey", "Action: Prev Profile");
                profileService.cycleBackward();
            },
            nextResult.binding.modifiers, nextResult.binding.vkCode,
            prevResult.binding.modifiers, prevResult.binding.vkCode
        );
        hotkeys.start();
        LOG_INFO("Hotkey", "Hotkeys bound: " + prevKeyStr + " and " + nextKeyStr);
#else
        kbd::IpcServer ipcServer([&](const std::string& cmd) {
            LOG_INFO("IPC", "Received command: " + cmd);
            if (cmd == "--next") profileService.cycleForward();
            else if (cmd == "--prev") profileService.cycleBackward();
            else if (cmd == "--slot1") profileService.switchHardwareSlot(0);
            else if (cmd == "--slot2") profileService.switchHardwareSlot(1);
            else if (cmd == "--slot3") profileService.switchHardwareSlot(2);
        });
        ipcServer.start();
#endif

        LOG_INFO("Daemon", "Daemon successfully started. Standing by for events.");

        // 10. Sleep main thread until OS shutdown signal is received
        {
            std::unique_lock<std::mutex> lock(g_shutdownMutex);
            g_shutdownCv.wait(lock, [] { return g_shouldTerminate; });
        }

        // 11. Graceful Teardown
        LOG_INFO("Daemon", "Initiating graceful teardown...");
#if defined(_WIN32)
        tray.stop();
        hotkeys.stop();
#else
        ipcServer.stop();
#endif
        uiManager->stop();
        deviceWatcher.stop();
        watcher.stop();
        keyboard.disconnect();
        LOG_INFO("Daemon", "Daemon terminated cleanly.");
    }
    catch (const kbd::KbdException& e) {
        LOG_ERROR("Fatal", std::string("KbdException: ") + e.what());
        return 2;
    }
    catch (const std::exception& e) {
        LOG_ERROR("Fatal", std::string("Unhandled std::exception: ") + e.what());
        return 1;
    }

    return 0;
}