#pragma once

#include "ui/TrayIconBase.h"

#include <memory>

namespace ui {

    // Windows tray backend (Shell_NotifyIcon). The icon lives on a dedicated thread with a hidden
    // message window; the native popup menu is built from the shared TrayMenu on demand. All Win32 types
    // stay in the .cpp (pimpl), so this header is safe to include on every platform. Left click =
    // onTrayActivated, middle click = onTraySecondaryActivated, right click = context menu.
    class Win32TrayIcon final : public TrayIconBase {
    public:
        Win32TrayIcon();
        ~Win32TrayIcon() override;

        Win32TrayIcon(const Win32TrayIcon&) = delete;
        Win32TrayIcon& operator=(const Win32TrayIcon&) = delete;
        Win32TrayIcon(Win32TrayIcon&&) = delete;
        Win32TrayIcon& operator=(Win32TrayIcon&&) = delete;

        bool start() override;
        void stop() override;
        [[nodiscard]] bool isRunning() const noexcept override;
        [[nodiscard]] std::string_view backendName() const noexcept override;

    private:
        void onStateChanged(unsigned changes) override;

        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace ui
