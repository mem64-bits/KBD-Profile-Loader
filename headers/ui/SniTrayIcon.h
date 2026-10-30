#pragma once

#include "ui/TrayIconBase.h"

#include <memory>
#include <string>

namespace ui {

    // Linux tray backend. Exports a StatusNotifierItem (the icon) and a com.canonical.dbusmenu (its context
    // menu) on the D-Bus session bus and registers with the desktop's StatusNotifierWatcher. That is the
    // tray protocol of KDE Plasma, GNOME (with the AppIndicator extension), and Sway / Hyprland / etc. with
    // waybar, and it works identically on Wayland and X11 sessions. Left click = onTrayActivated, middle
    // click = onTraySecondaryActivated; the menu is drawn by the desktop. All GIO types stay in the .cpp.
    //
    // Only compiled in when GIO was found at configure time (KBD_HAVE_SNI_TRAY); the factory falls back to
    // NullTrayIcon otherwise.
    class SniTrayIcon final : public TrayIconBase {
    public:
        // The instance number makes the bus name unique: org.kde.StatusNotifierItem-<pid>-<number>.
        explicit SniTrayIcon(int instanceNumber = 1);
        ~SniTrayIcon() override;

        SniTrayIcon(const SniTrayIcon&) = delete;
        SniTrayIcon& operator=(const SniTrayIcon&) = delete;
        SniTrayIcon(SniTrayIcon&&) = delete;
        SniTrayIcon& operator=(SniTrayIcon&&) = delete;

        // Connects to the session bus and starts exporting. Returns false if there is no session bus.
        // It still returns true when no tray host is running yet: the icon shows up as soon as one is.
        bool start() override;
        void stop() override;
        [[nodiscard]] bool isRunning() const noexcept override;
        [[nodiscard]] std::string_view backendName() const noexcept override;

        // The D-Bus name this instance owns (useful for diagnostics and tests).
        [[nodiscard]] std::string serviceName() const;

    private:
        void onStateChanged(unsigned changes) override;

        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace ui
