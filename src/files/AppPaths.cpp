#include "files/AppPaths.h"
#include <cstdlib>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <shlobj.h> // For SHGetFolderPathW
#else
    #include <unistd.h>
    #include <sys/types.h>
    #include <pwd.h>
#endif

namespace Files {

fs::path AppPaths::getProfilesDir() {
    return getDocumentsDir() / "Keychron Profiles";
}

fs::path AppPaths::getConfigFilePath() {
    return getAppDataDir() / "config.ini";
}

fs::path AppPaths::getLogFilePath() {
    return getAppDataDir() / "daemon.log";
}

fs::path AppPaths::getAppDataDir() {
#if defined(_WIN32)
    const char* appData = std::getenv("APPDATA");
    fs::path base = appData ? fs::path(appData) : fs::current_path();
    return base / "KBD_Profile_Loader";
#else
    const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
    if (xdgConfig) return fs::path(xdgConfig) / "KBD_Profile_Loader";
    const char* home = std::getenv("HOME");
    fs::path base = home ? fs::path(home) : fs::current_path();
    return base / ".config" / "KBD_Profile_Loader";
#endif
}

fs::path AppPaths::getDocumentsDir() {
#if defined(_WIN32)
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, 0, path))) {
        return fs::path(path);
    }
    const char* userProfile = std::getenv("USERPROFILE");
    return userProfile ? fs::path(userProfile) / "Documents" : fs::current_path();
#else
    const char* home = std::getenv("HOME");
    fs::path base = home ? fs::path(home) : fs::current_path();
    return base / "Documents";
#endif
}

fs::path AppPaths::getWindowsStartupDir() {
#if defined(_WIN32)
    const char* appData = std::getenv("APPDATA");
    fs::path base = appData ? fs::path(appData) : fs::current_path();
    return base / "Microsoft" / "Windows" / "Start Menu" / "Programs" / "Startup";
#else
    return {};
#endif
}

fs::path AppPaths::getSystemdUserDir() {
#if !defined(_WIN32)
    const char* home = std::getenv("HOME");
    fs::path base = home ? fs::path(home) : fs::current_path();
    return base / ".config" / "systemd" / "user";
#else
    return {};
#endif
}

void AppPaths::ensureDirectoriesExist() {
    fs::create_directories(getAppDataDir());
    fs::create_directories(getProfilesDir());
}

} // namespace Files