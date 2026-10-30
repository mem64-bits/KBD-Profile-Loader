#pragma once

namespace ui {

    // Operating-system services the UI needs, behind an interface so views and the tray stay
    // OS-agnostic: the Windows implementation uses ShellExecute and the Startup folder, the Linux one
    // uses xdg-open and an XDG autostart entry.
    class ISystemIntegration {
    public:
        virtual ~ISystemIntegration() = default;

        // Open in the system's file manager / default editor. Fire and forget.
        virtual void openProfilesFolder() = 0;
        virtual void openConfigFile() = 0;
        virtual void openLogFile() = 0;

        // Start the daemon automatically when the user logs in.
        [[nodiscard]] virtual bool isAutostartEnabled() const = 0;
        virtual bool setAutostartEnabled(bool enabled) = 0;
    };

} // namespace ui
