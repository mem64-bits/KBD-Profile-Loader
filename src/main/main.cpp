#include "kbd/KeyboardDevice.h"
#include "kbd/DeviceWatcher.h"
#include "kbd/KbdException.h"
#include "files/ProfileService.h"
#include "files/DirectoryWatcher.h"
#include "files/IniConfig.h"
#include "files/AppPaths.h"
#include "files/Logger.h"
#include "ui/AppModel.h"
#include "ui/DemoProfileGateway.h"
#include "ui/ProfileController.h"
#include "ui/ProfileServiceGateway.h"
#include "ui/SystemIntegration.h"
#include "ui/ThemeManager.h"
#include "ui/TrayIconFactory.h"
#include "ui/TrayPresenter.h"
#include "ui/imgui/ActuationPanel.h"
#include "ui/imgui/DashboardView.h"
#include "ui/imgui/ProfilesPanel.h"
#include "ui/imgui/SettingsPanel.h"
#include "ui/imgui/UIManager.h"

#include <iostream>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <csignal>
#include <thread>

#if defined(_WIN32)
    #include "kbd/HotkeyManager.h"
#else
    #include "kbd/IpcServer.h"
#endif

// Global condition variable to hold daemon execution until OS shutdown signal
std::condition_variable_any g_shutdownCv;
std::mutex                  g_shutdownMutex;
bool                        g_shouldTerminate = false;

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

static void printUsage() {
    std::cout <<
        "KBD_Profile_Loader - Keychron K2 HE profile daemon\n"
        "\n"
        "  --debug              console window + live terminal logging\n"
        "  --silent             headless background service, file logging only\n"
        "  --show               open the dashboard window at start-up (or in the running daemon)\n"
        "  --demo               run the UI against a simulated keyboard (no hardware needed)\n"
        "  --screenshot <file>  with --demo: render the dashboard to a PNG and exit\n"
        "  --tab <name>         first tab to show: profiles, actuation or settings\n"
#if !defined(_WIN32)
        "\n"
        "Commands for a running daemon (bind these to compositor / desktop shortcuts):\n"
        "  --next  --prev  --slot1  --slot2  --slot3  --toggle  --hide\n"
#endif
        ;
}

int main(int argc, char* argv[]) {
    // 1. Determine default debug mode based on CMake build type
#if defined(KBD_DEBUG_BUILD) || defined(_DEBUG) || !defined(NDEBUG)
    bool debugMode = true;  // Debug build: Console visible & live terminal logs ON by default
#else
    bool debugMode = false; // Release build: Silent background daemon, file logs only
#endif

    int         exitCode = 0;
    bool        showWindow = false;
    bool        demoMode = false;
    std::string screenshotPath;
    std::size_t firstTab = 0;

    // 2. Command-line argument overrides
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--debug") {
            debugMode = true;
        } else if (arg == "--silent" || arg == "--headless") {
            debugMode = false;
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else if (arg == "--demo") {
            demoMode = true;
        } else if (arg == "--screenshot" && i + 1 < argc) {
            screenshotPath = argv[++i];
            demoMode = true;
        } else if (arg == "--tab" && i + 1 < argc) {
            const std::string name = argv[++i];
            firstTab = name == "actuation" ? 1 : name == "settings" ? 2 : 0;
        } else if (arg == "--show") {
            showWindow = true;
#if !defined(_WIN32)
            // A daemon that is already running opens its window; otherwise this process becomes the daemon.
            if (kbd::IpcServer::sendCommand(arg)) return 0;
#endif
        }
#if !defined(_WIN32)
        else if (arg == "--next" || arg == "--prev" || arg == "--slot1" || arg == "--slot2" || arg == "--slot3" ||
                 arg == "--toggle" || arg == "--hide") {
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

        // 6. Connect to Hardware (USB-C Wired Priority), or fake a keyboard for --demo
        std::unique_ptr<kbd::KeyboardDevice>      keyboard;
        std::unique_ptr<kbd::ProfileService>      profileService;
        std::unique_ptr<Files::DirectoryWatcher>  watcher;
        std::unique_ptr<kbd::DeviceWatcher>       deviceWatcher;
        std::unique_ptr<ui::IProfileGateway>      gateway;

        if (demoMode) {
            LOG_INFO("Daemon", "Demo mode: using a simulated keyboard.");
            gateway = std::make_unique<ui::DemoProfileGateway>();
        } else {
            keyboard = std::make_unique<kbd::KeyboardDevice>();
            if (!keyboard->connect()) {
                LOG_WARN("Hardware", "Keychron K2 HE not found on startup. DeviceWatcher will probe continuously.");
            } else {
                LOG_INFO("Hardware", "Connected to Keychron K2 HE (" + keyboard->getDevicePath() + ")");
            }

            // 7. Profile Management & File Watcher
            profileService = std::make_unique<kbd::ProfileService>(*keyboard);
            profileService->loadAllFromDirectory(Files::AppPaths::getProfilesDir());

            watcher = std::make_unique<Files::DirectoryWatcher>(Files::AppPaths::getProfilesDir());
            deviceWatcher = std::make_unique<kbd::DeviceWatcher>(*keyboard);
            gateway = std::make_unique<ui::ProfileServiceGateway>(*profileService, *keyboard);
        }

        // 8. UI: theme, state model and the controller that owns every keyboard operation.
        //    The controller (not ProfileService) observes the watchers, so file / device events, hotkeys,
        //    IPC commands and UI clicks are all handled in order on one background thread.
        ui::ThemeManager& themes = ui::ThemeManager::getInstance();
        const auto themeFile = Files::AppPaths::getThemeFilePath();
        if (!demoMode && !std::filesystem::exists(themeFile)) {
            themes.saveToFile(themeFile); // write the defaults once, so there is something to edit
        }
        if (std::filesystem::exists(themeFile)) themes.loadFromFile(themeFile);

        ui::AppModel model;
        ui::ProfileController controller(*gateway, model);
        ui::SystemIntegration system;

        if (watcher && deviceWatcher) {
            watcher->addObserver(&controller);
            deviceWatcher->addObserver(&controller);
        }
        controller.start();
        if (watcher && deviceWatcher) {
            watcher->start();
            deviceWatcher->start();
        }

        // 9. Dear ImGui window host (runs on this, the main, thread) + the dashboard
        ui::UIManager* uiPointer = nullptr;
        ui::UIManagerOptions uiOptions;
        uiOptions.startVisible = showWindow || demoMode;
        uiOptions.onVisibilityChanged = [&controller](const bool visible) { controller.setPollingEnabled(visible); };
        bool screenshotSaved = false;
        if (!screenshotPath.empty()) {
            uiOptions.exitAfterFrames = 14; // let the layout settle before taking the picture
            uiOptions.onFrameRendered = [&](const int frame) {
                if (frame == uiOptions.exitAfterFrames - 1 && uiPointer != nullptr) {
                    screenshotSaved = uiPointer->saveScreenshot(screenshotPath);
                }
            };
        }
        ui::UIManager uiManager(uiOptions);
        uiPointer = &uiManager;

        auto dashboard = std::make_shared<ui::DashboardView>(model);
        dashboard->addPanel(std::make_unique<ui::ProfilesPanel>(controller, system));
        dashboard->addPanel(std::make_unique<ui::ActuationPanel>(controller));
        dashboard->addPanel(std::make_unique<ui::SettingsPanel>(system, themeFile));
        dashboard->selectTab(firstTab);
        uiManager.addView(dashboard);

        // 10. Shortcut Engine & Tray Integration
        std::unique_ptr<ui::ITrayIcon> tray = ui::createTrayIcon();
        ui::TrayPresenter trayPresenter(model, *tray, controller, system, uiManager, [] { triggerShutdown(); });
        bool trayRunning = false;
        if (!demoMode) {
            trayRunning = trayPresenter.start();
            LOG_INFO("Tray", std::string("System tray backend: ") + std::string(tray->backendName()) +
                             (trayRunning ? " (running)" : " (unavailable, running without a tray icon)"));
        }
        // Without a tray nothing could bring the window back after the user closes it.
        uiManager.setCloseButtonQuits(!trayRunning);
        if (!trayRunning && !demoMode) uiManager.showDashboard();

#if defined(_WIN32)
        kbd::HotkeyManager hotkeys(
            [&] {
                LOG_INFO("Hotkey", "Action: Next Profile");
                controller.cycleProfile(true);
            },
            [&] {
                LOG_INFO("Hotkey", "Action: Prev Profile");
                controller.cycleProfile(false);
            },
            nextResult.binding.modifiers, nextResult.binding.vkCode,
            prevResult.binding.modifiers, prevResult.binding.vkCode
        );
        hotkeys.start();
        LOG_INFO("Hotkey", "Hotkeys bound: " + prevKeyStr + " and " + nextKeyStr);
#else
        (void)nextResult; (void)prevResult; // global hotkeys are a Windows feature; Linux uses the IPC commands
        kbd::IpcServer ipcServer([&](const std::string& cmd) {
            LOG_INFO("IPC", "Received command: " + cmd);
            if (cmd == "--next") controller.cycleProfile(true);
            else if (cmd == "--prev") controller.cycleProfile(false);
            else if (cmd == "--slot1") controller.activateSlot(0);
            else if (cmd == "--slot2") controller.activateSlot(1);
            else if (cmd == "--slot3") controller.activateSlot(2);
            else if (cmd == "--show") uiManager.showDashboard();
            else if (cmd == "--hide") uiManager.hideDashboard();
            else if (cmd == "--toggle") uiManager.toggleDashboard();
        });
        if (!demoMode) ipcServer.start();
#endif

        LOG_INFO("Daemon", "Daemon successfully started. Standing by for events.");

        // 11. Run the window loop on the main thread until the OS or the tray asks us to stop. A helper
        //     thread turns the shutdown signal into a UI quit request. Without a display the UI cannot
        //     run, and the daemon then simply waits for the shutdown signal as it always did.
        std::jthread shutdownWatcher([&](const std::stop_token stopToken) {
            std::unique_lock<std::mutex> lock(g_shutdownMutex);
            g_shutdownCv.wait(lock, stopToken, [] { return g_shouldTerminate; });
            lock.unlock();
            uiManager.requestQuit();
        });

        if (!uiManager.run()) {
            if (!screenshotPath.empty()) {
                LOG_ERROR("UI", "Could not open a window to take the screenshot.");
                exitCode = 3;
            } else {
                std::unique_lock<std::mutex> lock(g_shutdownMutex);
                g_shutdownCv.wait(lock, [] { return g_shouldTerminate; });
            }
        } else if (!screenshotPath.empty() && !screenshotSaved) {
            LOG_ERROR("UI", "Could not write the screenshot to " + screenshotPath);
            exitCode = 3;
        }
        triggerShutdown(); // also lets the watcher thread finish when the window loop ended first

        // 12. Graceful Teardown
        LOG_INFO("Daemon", "Initiating graceful teardown...");
#if defined(_WIN32)
        hotkeys.stop();
#else
        ipcServer.stop();
#endif
        trayPresenter.stop();
        if (deviceWatcher) deviceWatcher->stop();
        if (watcher) watcher->stop();
        controller.stop();
        if (keyboard) keyboard->disconnect();
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

    return exitCode;
}
