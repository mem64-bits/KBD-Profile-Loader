#if !defined(_WIN32)

#include "ui/SystemIntegration.h"
#include "files/AppPaths.h"
#include "files/Logger.h"

#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

extern char** environ;

namespace fs = std::filesystem;

namespace ui {

namespace {

    constexpr const char* AUTOSTART_FILE_NAME = "kbd-profile-loader.desktop";

    // Runs `argv` without a shell (no quoting pitfalls with unusual paths) and reaps the child in the
    // background so it never lingers as a zombie.
    void spawnDetached(const std::vector<std::string>& arguments) {
        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1);
        for (const std::string& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
        argv.push_back(nullptr);

        pid_t pid = 0;
        const int result = posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ);
        if (result != 0) {
            LOG_WARN("System", "Could not run " + arguments[0] + ": " + std::error_code(result, std::generic_category()).message());
            return;
        }

        std::thread([pid] {
            int status = 0;
            waitpid(pid, &status, 0);
        }).detach();
    }

    // Desktop Entry spec: an Exec argument is double-quoted with \" \` \$ \\ escaped, and since the whole
    // value is a "string" every backslash is escaped once more; a literal '%' becomes "%%".
    std::string execArgument(const std::string& path) {
        std::string quoted = "\"";
        for (const char c : path) {
            if (c == '"' || c == '`' || c == '$' || c == '\\') quoted.push_back('\\');
            quoted.push_back(c);
        }
        quoted.push_back('"');

        std::string value;
        for (const char c : quoted) {
            if (c == '\\') value.push_back('\\');
            if (c == '%') value.push_back('%');
            value.push_back(c);
        }
        return value;
    }

    fs::path autostartEntryPath() {
        return Files::AppPaths::getAutostartDir() / AUTOSTART_FILE_NAME;
    }

} // namespace

void SystemIntegration::openPath(const fs::path& path) {
    spawnDetached({"xdg-open", path.string()});
}

fs::path SystemIntegration::resolveExecutable() const {
    if (!m_executablePath.empty()) return m_executablePath;

    std::error_code ec;
    const fs::path self = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::path{} : self;
}

bool SystemIntegration::isAutostartEnabled() const {
    std::error_code ec;
    return fs::exists(autostartEntryPath(), ec);
}

bool SystemIntegration::setAutostartEnabled(const bool enabled) {
    const fs::path entry = autostartEntryPath();
    std::error_code ec;

    if (!enabled) {
        fs::remove(entry, ec);
        if (ec) {
            LOG_WARN("System", "Could not remove the autostart entry: " + ec.message());
            return false;
        }
        LOG_INFO("System", "Autostart disabled (removed " + entry.string() + ").");
        return true;
    }

    const fs::path executable = resolveExecutable();
    if (executable.empty()) {
        LOG_WARN("System", "Could not determine the path of this program; autostart not enabled.");
        return false;
    }

    fs::create_directories(entry.parent_path(), ec);
    std::ofstream file(entry, std::ios::trunc);
    if (!file.is_open()) {
        LOG_WARN("System", "Could not write " + entry.string());
        return false;
    }

    file << "[Desktop Entry]\n"
         << "Type=Application\n"
         << "Name=Keychron K2 HE Profile Daemon\n"
         << "Comment=Switch Keychron K2 HE profiles from the system tray\n"
         << "Exec=" << execArgument(executable.string()) << "\n"
         << "Icon=input-keyboard\n"
         << "Terminal=false\n"
         << "X-GNOME-Autostart-enabled=true\n";
    file.flush();
    if (!file.good()) return false;

    LOG_INFO("System", "Autostart enabled (" + entry.string() + ").");
    return true;
}

} // namespace ui

#endif // !_WIN32
