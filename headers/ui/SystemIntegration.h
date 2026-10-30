#pragma once

#include "ui/ISystemIntegration.h"

#include <filesystem>

namespace ui {

    // ISystemIntegration for the running OS. The portable part lives in SystemIntegration.cpp; the
    // platform-specific functions are in SystemIntegrationWin32.cpp (ShellExecute, Startup-folder shortcut)
    // and SystemIntegrationPosix.cpp (xdg-open, XDG autostart entry).
    class SystemIntegration final : public ISystemIntegration {
    public:
        // `executablePath` is what the autostart entry launches; empty = this process's own executable.
        explicit SystemIntegration(std::filesystem::path executablePath = {});
        ~SystemIntegration() override;

        SystemIntegration(const SystemIntegration&) = delete;
        SystemIntegration& operator=(const SystemIntegration&) = delete;
        SystemIntegration(SystemIntegration&&) = delete;
        SystemIntegration& operator=(SystemIntegration&&) = delete;

        void openProfilesFolder() override;
        void openConfigFile() override;
        void openLogFile() override;
        [[nodiscard]] bool isAutostartEnabled() const override;
        bool setAutostartEnabled(bool enabled) override;

    private:
        static void openPath(const std::filesystem::path& path);
        [[nodiscard]] std::filesystem::path resolveExecutable() const;

        std::filesystem::path m_executablePath;
    };

} // namespace ui
