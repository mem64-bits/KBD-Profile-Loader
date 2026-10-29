#pragma once

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace Files {

    class AppPaths {
    public:
        // User-facing profiles directory: ~/Documents/Keychron Profiles/
        [[nodiscard]] static fs::path getProfilesDir();

        // Application internal configuration: config.ini
        [[nodiscard]] static fs::path getConfigFilePath();

        // Daemon runtime log: daemon.log
        [[nodiscard]] static fs::path getLogFilePath();

        /*Windows: %APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup*/
        [[nodiscard]] static fs::path getWindowsStartupDir();

        // Linux: ~/.config/systemd/user/
        [[nodiscard]] static fs::path getSystemdUserDir();

        // Creates required directories if they don't exist
        static void ensureDirectoriesExist();

    private:
        [[nodiscard]] static fs::path getAppDataDir();
        [[nodiscard]] static fs::path getDocumentsDir();
    };

} // namespace Files