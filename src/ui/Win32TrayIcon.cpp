#if defined(_WIN32)

#include "ui/Win32TrayIcon.h"
#include "ui/TrayIconRenderer.h"
#include "files/Logger.h"

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
// This file calls the wide (W) Win32 functions explicitly; UNICODE makes macros such as IDI_APPLICATION
// resolve to their wide variants too, instead of the ANSI ones.
#ifndef UNICODE
    #define UNICODE
#endif
#ifndef _UNICODE
    #define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <thread>

namespace ui {

namespace {

    constexpr UINT WM_TRAY_CALLBACK = WM_APP + 1; // mouse events on the icon
    constexpr UINT WM_TRAY_REFRESH  = WM_APP + 2; // state changed on another thread
    constexpr UINT TRAY_ICON_ID     = 1;
    constexpr wchar_t WINDOW_CLASS[] = L"KBD_Profile_Loader_TrayWindow";

    std::wstring widen(const std::string& utf8) {
        if (utf8.empty()) return {};
        const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        if (length <= 0) return {};
        std::wstring wide(static_cast<std::size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), length);
        return wide;
    }

    // '&' marks an accelerator in Win32 menus; a literal ampersand has to be doubled.
    std::wstring menuLabel(const std::string& utf8) {
        std::wstring label;
        for (const wchar_t c : widen(utf8)) {
            label.push_back(c);
            if (c == L'&') label.push_back(L'&');
        }
        return label;
    }

    // Builds an HICON from straight-alpha RGBA pixels (same technique GLFW uses for window icons).
    HICON createIcon(const TrayImage& image) {
        if (image.width <= 0 || image.height <= 0) return nullptr;

        BITMAPV5HEADER header{};
        header.bV5Size        = sizeof(header);
        header.bV5Width       = image.width;
        header.bV5Height      = -image.height; // top-down
        header.bV5Planes      = 1;
        header.bV5BitCount    = 32;
        header.bV5Compression = BI_BITFIELDS;
        header.bV5RedMask     = 0x00FF0000;
        header.bV5GreenMask   = 0x0000FF00;
        header.bV5BlueMask    = 0x000000FF;
        header.bV5AlphaMask   = 0xFF000000;

        void* pixels = nullptr;
        HDC screen = GetDC(nullptr);
        HBITMAP color = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &pixels, nullptr, 0);
        ReleaseDC(nullptr, screen);
        if (color == nullptr || pixels == nullptr) return nullptr;

        auto* target = static_cast<unsigned char*>(pixels);
        for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
            target[i + 0] = image.rgba[i + 2]; // B
            target[i + 1] = image.rgba[i + 1]; // G
            target[i + 2] = image.rgba[i + 0]; // R
            target[i + 3] = image.rgba[i + 3]; // A
        }

        HBITMAP mask = CreateBitmap(image.width, image.height, 1, 1, nullptr);
        ICONINFO info{};
        info.fIcon    = TRUE;
        info.hbmMask  = mask;
        info.hbmColor = color;
        HICON icon = CreateIconIndirect(&info);

        DeleteObject(color);
        DeleteObject(mask);
        return icon;
    }

    // The smallest rendered image that is at least as large as the tray's icon size (or the largest one if
    // none is), scaled to fit exactly.
    TrayImage bestFit(const std::vector<TrayImage>& images, const int wanted) {
        const TrayImage* best = nullptr;
        for (const TrayImage& image : images) {
            if (image.width <= 0) continue;
            if (best == nullptr) {
                best = &image;
                continue;
            }

            const bool imageFits = image.width >= wanted;
            const bool bestFits  = best->width >= wanted;
            if (imageFits && (!bestFits || image.width < best->width)) {
                best = &image; // a smaller image that still fits
            } else if (!imageFits && !bestFits && image.width > best->width) {
                best = &image; // nothing fits yet: prefer the largest
            }
        }
        if (best == nullptr) return {};
        return best->width == wanted ? *best : scaleTrayImage(*best, wanted);
    }

    // Copies as much of `text` as fits into the fixed-size, NUL-terminated tooltip buffer.
    template <std::size_t N>
    void copyTooltip(wchar_t (&target)[N], const std::wstring& text) {
        const std::size_t count = std::min(text.size(), N - 1);
        std::copy_n(text.begin(), count, target);
        target[count] = L'\0';
    }

} // namespace

struct Win32TrayIcon::Impl {
    explicit Impl(Win32TrayIcon& tray) : owner(tray) {}

    Win32TrayIcon&     owner;
    std::jthread       thread;
    std::atomic<HWND>  window{nullptr};
    std::atomic<bool>  running{false};

    // Touched only on the tray thread:
    HICON              icon{nullptr};
    UINT               taskbarCreated{0};

    void run(std::promise<bool> ready);
    bool addIcon();
    void refresh();
    void showMenu();
    void destroyIcon();

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
};

LRESULT CALLBACK Win32TrayIcon::Impl::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) return DefWindowProcW(hwnd, message, wParam, lParam);

    if (message == WM_TRAY_CALLBACK) {
        switch (LOWORD(lParam)) {
            case WM_LBUTTONUP: self->owner.notifyActivated(); break;
            case WM_MBUTTONUP: self->owner.notifySecondaryActivated(); break;
            case WM_RBUTTONUP: self->showMenu(); break;
            default: break;
        }
        return 0;
    }
    if (message == WM_TRAY_REFRESH) {
        self->refresh();
        return 0;
    }
    if (message == self->taskbarCreated && message != 0) {
        // Explorer restarted (or the taskbar was not ready when we started): put the icon back.
        self->addIcon();
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool Win32TrayIcon::Impl::addIcon() {
    HWND hwnd = window.load();
    if (hwnd == nullptr) return false;

    NOTIFYICONDATAW data{};
    data.cbSize           = sizeof(data);
    data.hWnd             = hwnd;
    data.uID              = TRAY_ICON_ID;
    data.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = WM_TRAY_CALLBACK;

    const auto state = owner.state();
    destroyIcon();
    icon = createIcon(bestFit(state.icons, GetSystemMetrics(SM_CXSMICON)));
    data.hIcon = icon != nullptr ? icon : LoadIconW(nullptr, IDI_APPLICATION);

    const std::wstring tip = widen(state.title + (state.description.empty() ? "" : "\n" + state.description));
    copyTooltip(data.szTip, tip);

    // The first NIM_ADD fails if the taskbar does not exist yet (autostart at logon); the TaskbarCreated
    // broadcast calls us again once it does.
    return Shell_NotifyIconW(NIM_ADD, &data) == TRUE;
}

void Win32TrayIcon::Impl::refresh() {
    HWND hwnd = window.load();
    if (hwnd == nullptr) return;

    const auto state = owner.state();

    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd   = hwnd;
    data.uID    = TRAY_ICON_ID;
    data.uFlags = NIF_ICON | NIF_TIP;

    HICON fresh = createIcon(bestFit(state.icons, GetSystemMetrics(SM_CXSMICON)));
    data.hIcon = fresh != nullptr ? fresh : (icon != nullptr ? icon : LoadIconW(nullptr, IDI_APPLICATION));

    const std::wstring tip = widen(state.title + (state.description.empty() ? "" : "\n" + state.description));
    copyTooltip(data.szTip, tip);

    if (Shell_NotifyIconW(NIM_MODIFY, &data) == TRUE) {
        if (fresh != nullptr) {
            if (icon != nullptr) DestroyIcon(icon);
            icon = fresh;
        }
    } else {
        if (fresh != nullptr) DestroyIcon(fresh);
        addIcon(); // the shell forgot about us (for example after Explorer crashed): add it again
    }
}

void Win32TrayIcon::Impl::showMenu() {
    HWND hwnd = window.load();
    if (hwnd == nullptr) return;

    const TrayMenu menu = owner.state().menu;
    HMENU popup = CreatePopupMenu();
    if (popup == nullptr) return;

    for (const TrayMenuItem& item : menu.items) {
        std::wstring label = menuLabel(item.label);

        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask  = MIIM_ID | MIIM_FTYPE | MIIM_STATE | MIIM_STRING;
        info.wID    = static_cast<UINT>(item.id);

        switch (item.kind) {
            case TrayMenuItem::Kind::Separator:
                info.fType  = MFT_SEPARATOR;
                info.fState = MFS_ENABLED;
                break;
            case TrayMenuItem::Kind::Header:
                info.fType  = MFT_STRING;
                info.fState = MFS_DISABLED;
                break;
            case TrayMenuItem::Kind::Radio:
                info.fType  = MFT_STRING | MFT_RADIOCHECK;
                info.fState = (item.enabled ? MFS_ENABLED : MFS_DISABLED) | (item.checked ? MFS_CHECKED : MFS_UNCHECKED);
                break;
            case TrayMenuItem::Kind::Check:
                info.fType  = MFT_STRING;
                info.fState = (item.enabled ? MFS_ENABLED : MFS_DISABLED) | (item.checked ? MFS_CHECKED : MFS_UNCHECKED);
                break;
            case TrayMenuItem::Kind::Action:
                info.fType  = MFT_STRING;
                info.fState = item.enabled ? MFS_ENABLED : MFS_DISABLED;
                break;
        }
        if (item.kind != TrayMenuItem::Kind::Separator) {
            info.dwTypeData = label.data();
            info.cch        = static_cast<UINT>(label.size());
        }
        InsertMenuItemW(popup, GetMenuItemCount(popup), TRUE, &info);
    }

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(hwnd); // without this the menu would not close when the user clicks elsewhere

    const UINT chosen = static_cast<UINT>(TrackPopupMenu(popup, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                                          cursor.x, cursor.y, 0, hwnd, nullptr));
    PostMessageW(hwnd, WM_NULL, 0, 0); // standard workaround so the menu dismisses cleanly (KB135788)
    DestroyMenu(popup);

    if (chosen != 0) owner.notifyMenuItemSelected(static_cast<int>(chosen));
}

void Win32TrayIcon::Impl::destroyIcon() {
    if (icon != nullptr) {
        DestroyIcon(icon);
        icon = nullptr;
    }
}

void Win32TrayIcon::Impl::run(std::promise<bool> ready) {
    HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize        = sizeof(windowClass);
    windowClass.lpfnWndProc   = &Impl::windowProc;
    windowClass.hInstance     = instance;
    windowClass.lpszClassName = WINDOW_CLASS;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        LOG_ERROR("Tray", "Could not register the tray window class.");
        ready.set_value(false);
        return;
    }

    // A hidden top-level window (not message-only): only top-level windows receive the TaskbarCreated broadcast.
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, WINDOW_CLASS, L"KBD Profile Loader tray", WS_POPUP,
                                0, 0, 0, 0, nullptr, nullptr, instance, this);
    if (hwnd == nullptr) {
        LOG_ERROR("Tray", "Could not create the tray window.");
        UnregisterClassW(WINDOW_CLASS, instance);
        ready.set_value(false);
        return;
    }

    window.store(hwnd);
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    running.store(true);

    // Report success before touching the taskbar: Shell_NotifyIcon can block for several seconds while the
    // shell starts (for example when the daemon autostarts at logon), and that must not delay start-up.
    ready.set_value(true);

    if (!addIcon()) {
        LOG_WARN("Tray", "The taskbar did not accept the tray icon yet; it will be added when the taskbar appears.");
    } else {
        LOG_INFO("Tray", "Windows system tray icon created.");
    }

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    running.store(false);
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd   = hwnd;
    data.uID    = TRAY_ICON_ID;
    Shell_NotifyIconW(NIM_DELETE, &data);
    destroyIcon();

    window.store(nullptr);
    UnregisterClassW(WINDOW_CLASS, instance);
}

Win32TrayIcon::Win32TrayIcon() : m_impl(std::make_unique<Impl>(*this)) {}

Win32TrayIcon::~Win32TrayIcon() {
    stop();
}

bool Win32TrayIcon::start() {
    if (m_impl->running.load()) return true;
    if (m_impl->thread.joinable()) return false; // a previous start() is still shutting down

    std::promise<bool> ready;
    std::future<bool> result = ready.get_future();
    m_impl->thread = std::jthread([impl = m_impl.get(), promise = std::move(ready)](const std::stop_token&) mutable {
        impl->run(std::move(promise));
    });

    if (result.wait_for(std::chrono::seconds(5)) != std::future_status::ready || !result.get()) {
        stop();
        return false;
    }
    return true;
}

void Win32TrayIcon::stop() {
    if (!m_impl->thread.joinable()) return;

    if (HWND hwnd = m_impl->window.load()) PostMessageW(hwnd, WM_CLOSE, 0, 0);
    m_impl->thread.join();
}

bool Win32TrayIcon::isRunning() const noexcept {
    return m_impl->running.load();
}

std::string_view Win32TrayIcon::backendName() const noexcept {
    return "win32";
}

void Win32TrayIcon::onStateChanged(unsigned) {
    // Called from any thread: let the tray thread apply the cached state.
    if (HWND hwnd = m_impl->window.load()) PostMessageW(hwnd, WM_TRAY_REFRESH, 0, 0);
}

} // namespace ui

#endif // _WIN32
