#if defined(_WIN32)

#include "ui/TrayManager.h"
#include "files/ProfileService.h"
#include "files/AppPaths.h"
#include "files/Logger.h"
#include "ui/UIManager.h" // Links with your new UI layer!

namespace kbd {

constexpr UINT WM_TRAYICON = WM_USER + 200;

// Context Menu IDs for Utility Actions
constexpr UINT IDM_SHOW_UI       = 2001;
constexpr UINT IDM_OPEN_PROFILES = 2002;
constexpr UINT IDM_OPEN_CONFIG   = 2003;
constexpr UINT IDM_OPEN_LOGS     = 2004;
constexpr UINT IDM_STARTUP_TOGGLE= 2005;
constexpr UINT IDM_EXIT          = 2006;

TrayManager::TrayManager(ProfileService& profileService, UI::UIManager& uiManager, ShutdownCallback onShutdown)
    : m_profileService(profileService),
      m_uiManager(uiManager),
      m_onShutdown(std::move(onShutdown))
{}

TrayManager::~TrayManager() {
    stop();
}

void TrayManager::start() {
    if (m_running.load(std::memory_order_acquire)) return;
    m_running.store(true, std::memory_order_release);

    m_thread = std::jthread([this](std::stop_token stopToken) {
        threadWorker(stopToken);
    });
}

void TrayManager::stop() {
    if (!m_running.exchange(false, std::memory_order_acq_rel)) return;

    if (m_hwnd) {
        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
    }
    if (m_threadId != 0) {
        PostThreadMessageW(m_threadId, WM_QUIT, 0, 0);
    }

    if (m_thread.joinable()) {
        m_thread.request_stop();
        m_thread.join();
    }
}

void TrayManager::updateStatus(const std::string& profileName, int activeSlot) {
    m_currentProfileName = profileName;
    m_currentSlot = activeSlot;

    if (!m_hwnd) return;

    // Hover tooltip: "Keychron K2 HE: Slot X [ProfileName]"
    std::wstring tip = L"Keychron K2 HE: Slot " + std::to_wstring(activeSlot + 1) +
                       L" [" + std::wstring(profileName.begin(), profileName.end()) + L"]";

    wcsncpy_s(m_nid.szTip, tip.c_str(), _TRUNCATE);
    m_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

bool TrayManager::isStartupShortcutPresent() const {
    fs::path startupLink = Files::AppPaths::getWindowsStartupDir() / "Keychron Daemon.lnk";
    return fs::exists(startupLink);
}

void TrayManager::toggleStartupShortcut() {
    fs::path startupLink = Files::AppPaths::getWindowsStartupDir() / "Keychron Daemon.lnk";
    if (fs::exists(startupLink)) {
        fs::remove(startupLink);
        LOG_INFO("Tray", "Disabled Windows Auto-Start (Removed shortcut from Startup folder).");
    } else {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);

        std::wstring psCmd = L"powershell -NoProfile -Command \"$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut('" +
                             startupLink.wstring() + L"'); $s.TargetPath = '" + std::wstring(exePath) + L"'; $s.Save()\"";

        _wsystem(psCmd.c_str());
        LOG_INFO("Tray", "Enabled Windows Auto-Start (Created shortcut in Startup folder).");
    }
}

void TrayManager::showContextMenu() {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    // Header showing active profile
    std::wstring header = L"Keychron K2 HE (Slot " + std::to_wstring(m_currentSlot + 1) + L")";
    AppendMenuW(hMenu, MF_STRING | MF_DISABLED, 0, header.c_str());
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    // 1-Click trigger to open the ImGui Dashboard
    AppendMenuW(hMenu, MF_STRING, IDM_SHOW_UI, L"Open Profile Dashboard (ImGui)...");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    // Shell shortcuts
    AppendMenuW(hMenu, MF_STRING, IDM_OPEN_PROFILES, L"Open Profiles Folder (Documents)...");
    AppendMenuW(hMenu, MF_STRING, IDM_OPEN_CONFIG,   L"Edit config.ini...");
    AppendMenuW(hMenu, MF_STRING, IDM_OPEN_LOGS,     L"View daemon.log...");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    // Startup Toggle [✓]
    UINT startupFlag = isStartupShortcutPresent() ? MF_CHECKED : MF_UNCHECKED;
    AppendMenuW(hMenu, MF_STRING | startupFlag, IDM_STARTUP_TOGGLE, L"Run on Windows Startup");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    // Exit
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit Daemon");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(m_hwnd);

    UINT cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, m_hwnd, NULL);
    DestroyMenu(hMenu);

    if (cmd == IDM_SHOW_UI) {
        m_uiManager.toggleProfilePicker(); // Summons ImGui window!
    } else if (cmd == IDM_OPEN_PROFILES) {
        ShellExecuteW(NULL, L"open", Files::AppPaths::getProfilesDir().c_str(), NULL, NULL, SW_SHOWNORMAL);
    } else if (cmd == IDM_OPEN_CONFIG) {
        ShellExecuteW(NULL, L"open", Files::AppPaths::getConfigFilePath().c_str(), NULL, NULL, SW_SHOWNORMAL);
    } else if (cmd == IDM_OPEN_LOGS) {
        ShellExecuteW(NULL, L"open", Files::AppPaths::getLogFilePath().c_str(), NULL, NULL, SW_SHOWNORMAL);
    } else if (cmd == IDM_STARTUP_TOGGLE) {
        toggleStartupShortcut();
    } else if (cmd == IDM_EXIT) {
        if (m_onShutdown) m_onShutdown();
    }
}

LRESULT CALLBACK TrayManager::windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_CREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }

    TrayManager* pThis = reinterpret_cast<TrayManager*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (!pThis) return DefWindowProcW(hwnd, uMsg, wParam, lParam);

    if (uMsg == WM_TRAYICON) {
        if (LOWORD(lParam) == WM_LBUTTONUP) {
            // Left-Click: Summons the ImGui Quick Profile Switcher!
            pThis->m_uiManager.toggleProfilePicker();
        } else if (LOWORD(lParam) == WM_RBUTTONUP) {
            // Right-Click: Opens Utility Context Menu
            pThis->showContextMenu();
        } else if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
            // Double-Click: Fast cycle forward
            pThis->m_profileService.cycleForward();
        }
        return 0;
    } else if (uMsg == WM_DESTROY) {
        Shell_NotifyIconW(NIM_DELETE, &pThis->m_nid);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

void TrayManager::threadWorker(std::stop_token stopToken) {
    m_threadId = GetCurrentThreadId();

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc   = windowProc;
    wc.hInstance     = GetModuleHandle(NULL);
    wc.lpszClassName = L"KBD_Tray_Bridge_Class";

    RegisterClassExW(&wc);

    // Hidden message window to receive tray events
    m_hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"KBD_Tray_Bridge_Window", 0,
        0, 0, 0, 0, NULL, NULL, wc.hInstance, this
    );

    if (!m_hwnd) {
        LOG_ERROR("Tray", "Failed to create hidden window for tray bridge.");
        return;
    }

    std::memset(&m_nid, 0, sizeof(m_nid));
    m_nid.cbSize           = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd             = m_hwnd;
    m_nid.uID              = 1;
    m_nid.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon            = LoadIcon(NULL, IDI_APPLICATION); // Standard default icon

    std::wstring initialTip = L"Keychron K2 HE Native Daemon";
    wcsncpy_s(m_nid.szTip, initialTip.c_str(), _TRUNCATE);

    Shell_NotifyIconW(NIM_ADD, &m_nid);
    LOG_INFO("Tray", "Windows system tray bridge initialized.");

    MSG msg;
    while (!stopToken.stop_requested() && GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Shell_NotifyIconW(NIM_DELETE, &m_nid);
    DestroyWindow(m_hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    m_hwnd = nullptr;
    m_threadId = 0;
}

} // namespace kbd

#endif // _WIN32