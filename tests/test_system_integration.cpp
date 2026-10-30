#include <gtest/gtest.h>

#if !defined(_WIN32)

#include "files/AppPaths.h"
#include "support/TempDir.h"
#include "ui/SystemIntegration.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

// The POSIX implementation, run against a throw-away XDG_CONFIG_HOME / PATH so nothing on the machine
// is touched: a fake `xdg-open` records the arguments it is started with.

namespace {

    class EnvGuard {
    public:
        EnvGuard(const char* name, const std::string& value) : m_name(name) {
            if (const char* old = std::getenv(name)) m_old = old, m_hadOld = true;
            setenv(name, value.c_str(), 1);
        }
        ~EnvGuard() {
            if (m_hadOld) setenv(m_name, m_old.c_str(), 1);
            else unsetenv(m_name);
        }
        EnvGuard(const EnvGuard&) = delete;
        EnvGuard& operator=(const EnvGuard&) = delete;

    private:
        const char* m_name;
        std::string m_old;
        bool        m_hadOld = false;
    };

    std::string readFile(const std::filesystem::path& path) {
        std::ifstream file(path);
        std::stringstream text;
        text << file.rdbuf();
        return text.str();
    }

    class SystemIntegrationTest : public ::testing::Test {
    protected:
        test::TempDir home;
        EnvGuard      xdg{"XDG_CONFIG_HOME", home.path().string()};
        EnvGuard      homeVar{"HOME", home.path().string()};
    };

}

TEST_F(SystemIntegrationTest, AutostartEntryIsCreatedAndRemoved) {
    ui::SystemIntegration system("/opt/kbd/KBD_Profile_Loader");
    const auto entry = home / "autostart/kbd-profile-loader.desktop";

    EXPECT_FALSE(system.isAutostartEnabled());

    ASSERT_TRUE(system.setAutostartEnabled(true));
    EXPECT_TRUE(system.isAutostartEnabled());
    ASSERT_TRUE(std::filesystem::exists(entry)) << "created under $XDG_CONFIG_HOME/autostart";

    const std::string text = readFile(entry);
    EXPECT_NE(text.find("[Desktop Entry]"), std::string::npos);
    EXPECT_NE(text.find("Type=Application"), std::string::npos);
    EXPECT_NE(text.find("Exec=\"/opt/kbd/KBD_Profile_Loader\""), std::string::npos) << text;
    EXPECT_NE(text.find("Terminal=false"), std::string::npos);

    ASSERT_TRUE(system.setAutostartEnabled(false));
    EXPECT_FALSE(system.isAutostartEnabled());
    EXPECT_FALSE(std::filesystem::exists(entry));
}

TEST_F(SystemIntegrationTest, DisablingWhenNothingIsInstalledSucceeds) {
    ui::SystemIntegration system("/opt/kbd/KBD_Profile_Loader");
    EXPECT_TRUE(system.setAutostartEnabled(false));
}

TEST_F(SystemIntegrationTest, EnablingTwiceIsIdempotent) {
    ui::SystemIntegration system("/opt/kbd/KBD_Profile_Loader");
    ASSERT_TRUE(system.setAutostartEnabled(true));
    ASSERT_TRUE(system.setAutostartEnabled(true));
    EXPECT_TRUE(system.isAutostartEnabled());
}

TEST_F(SystemIntegrationTest, ExecLineEscapesPathsAccordingToTheDesktopEntrySpec) {
    ui::SystemIntegration system("/opt/My Apps/100%/kbd$tool/KBD_Profile_Loader");
    ASSERT_TRUE(system.setAutostartEnabled(true));

    const std::string text = readFile(home / "autostart/kbd-profile-loader.desktop");

    // Quoted for the spaces; '$' escaped (\$ -> \\$ once the string escaping is applied); '%' doubled.
    EXPECT_NE(text.find("Exec=\"/opt/My Apps/100%%/kbd\\\\$tool/KBD_Profile_Loader\""), std::string::npos) << text;
}

TEST_F(SystemIntegrationTest, OwnExecutableIsUsedWhenNoneIsGiven) {
    ui::SystemIntegration system;
    ASSERT_TRUE(system.setAutostartEnabled(true));

    const std::string text = readFile(home / "autostart/kbd-profile-loader.desktop");
    EXPECT_NE(text.find("KBD_Unit_Tests"), std::string::npos) << "resolved through /proc/self/exe: " << text;
}

TEST_F(SystemIntegrationTest, OpeningPathsLaunchesXdgOpenWithoutAShell) {
    // A fake xdg-open that appends its arguments to a log file.
    const auto bin = home / "bin";
    std::filesystem::create_directories(bin);
    const auto log = home / "xdg-open.log";
    {
        std::ofstream script(bin / "xdg-open");
        script << "#!/bin/sh\necho \"$@\" >> \"" << log.string() << "\"\n";
    }
    std::filesystem::permissions(bin / "xdg-open", std::filesystem::perms::owner_all);

    const char* oldPath = std::getenv("PATH");
    EnvGuard path("PATH", bin.string() + ":" + (oldPath ? oldPath : ""));

    ui::SystemIntegration system;
    system.openProfilesFolder();
    system.openConfigFile();
    system.openLogFile();

    // The children run asynchronously: wait for all three lines.
    std::string text;
    for (int i = 0; i < 200 && std::count(text.begin(), text.end(), '\n') < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        text = readFile(log);
    }
    EXPECT_NE(text.find(Files::AppPaths::getProfilesDir().string()), std::string::npos) << text;
    EXPECT_NE(text.find(Files::AppPaths::getConfigFilePath().string()), std::string::npos) << text;
    EXPECT_NE(text.find(Files::AppPaths::getLogFilePath().string()), std::string::npos) << text;
}

TEST_F(SystemIntegrationTest, AppPathsHonourXdgConfigHome) {
    EXPECT_EQ(Files::AppPaths::getAutostartDir(), home / "autostart");
    EXPECT_EQ(Files::AppPaths::getThemeFilePath(), home / "KBD_Profile_Loader/theme.json");
}

#else // _WIN32

#include "files/AppPaths.h"
#include "support/TempDir.h"
#include "ui/SystemIntegration.h"

#include <cstdlib>

// The Windows implementation: a real Startup-folder shortcut created through the IShellLink COM API, in a
// throw-away %APPDATA% so the machine's own Startup folder is never touched.

TEST(SystemIntegrationWindowsTest, StartupShortcutIsCreatedAndRemoved) {
    test::TempDir appData;
    const char* oldAppData = std::getenv("APPDATA");
    const std::string previous = oldAppData != nullptr ? oldAppData : "";
    _putenv_s("APPDATA", appData.path().string().c_str());

    const auto startupFolder = Files::AppPaths::getWindowsStartupDir();
    std::filesystem::create_directories(startupFolder);
    const auto shortcut = startupFolder / "Keychron Daemon.lnk"; // same name the installer uses

    ui::SystemIntegration system(appData.path() / "KBD_Profile_Loader.exe");
    EXPECT_FALSE(system.isAutostartEnabled());

    EXPECT_TRUE(system.setAutostartEnabled(true));
    EXPECT_TRUE(std::filesystem::exists(shortcut));
    EXPECT_TRUE(system.isAutostartEnabled());
    EXPECT_GT(std::filesystem::file_size(shortcut), 0u);

    EXPECT_TRUE(system.setAutostartEnabled(true)) << "enabling twice must be harmless";

    EXPECT_TRUE(system.setAutostartEnabled(false));
    EXPECT_FALSE(std::filesystem::exists(shortcut));
    EXPECT_FALSE(system.isAutostartEnabled());
    EXPECT_TRUE(system.setAutostartEnabled(false)) << "disabling when nothing is there succeeds";

    _putenv_s("APPDATA", previous.c_str());
}

#endif
