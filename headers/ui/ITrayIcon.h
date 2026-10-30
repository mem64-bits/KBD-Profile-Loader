#pragma once

#include "ui/TrayMenu.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ui {

    // Straight-alpha RGBA8 image, rows stored top to bottom (width * height * 4 bytes).
    struct TrayImage {
        int                       width{0};
        int                       height{0};
        std::vector<std::uint8_t> rgba;

        bool operator==(const TrayImage&) const = default;
    };

    class ITrayObserver {
    public:
        virtual ~ITrayObserver() = default;

        // All three may be invoked from the tray backend's own thread.
        virtual void onTrayActivated() = 0;                // primary click
        virtual void onTraySecondaryActivated() = 0;       // middle click
        virtual void onTrayMenuItemSelected(int itemId) = 0;
    };

    // An OS-agnostic system tray icon. Windows draws it with Shell_NotifyIcon, Linux (Wayland and X11)
    // exposes it over D-Bus as a StatusNotifierItem; everything above this interface is shared code.
    class ITrayIcon {
    public:
        virtual ~ITrayIcon() = default;

        // Creates the icon. Returns false when this session offers no tray (the app then runs without one).
        virtual bool start() = 0;
        virtual void stop() = 0;
        [[nodiscard]] virtual bool isRunning() const noexcept = 0;
        [[nodiscard]] virtual std::string_view backendName() const noexcept = 0;

        // Thread-safe, callable before or after start(). The latest values are what the user sees, and are
        // re-applied automatically if the tray host restarts. Unchanged values are ignored.
        virtual void setIcons(std::vector<TrayImage> images) = 0; // several sizes; each backend uses what it needs
        virtual void setTooltip(std::string title, std::string description) = 0;
        virtual void setMenu(TrayMenu menu) = 0;

        virtual void addObserver(ITrayObserver* observer) = 0;
        virtual void removeObserver(ITrayObserver* observer) = 0;
    };

} // namespace ui
