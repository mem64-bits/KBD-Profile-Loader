#include <gtest/gtest.h>

#if defined(_WIN32)

#include "ui/TrayIconRenderer.h"
#include "ui/TrayMenuBuilder.h"
#include "ui/Win32TrayIcon.h"

#include <chrono>
#include <thread>

// Lifecycle of the Windows tray backend: its message-window thread must start, accept state from other
// threads and shut down without hanging. (Whether the taskbar shows the icon depends on the shell, so the
// tests only require that the backend itself works; a missing taskbar is handled by TaskbarCreated.)

namespace {

    ui::TrayMenu sampleMenu() {
        ui::AppSnapshot state;
        state.device = {true, kbd::TransportMode::Wired, "path"};
        state.activeSlot = 0;
        state.slots[0] = {true, "Default & more", "default.json", std::nullopt}; // '&' must survive as text
        return ui::tray::buildMenu(state, false);
    }

}

TEST(Win32TrayIconTest, StartsAcceptsStateAndStopsCleanly) {
    ui::Win32TrayIcon tray;
    ASSERT_TRUE(tray.start());
    EXPECT_TRUE(tray.isRunning());
    EXPECT_EQ(tray.backendName(), "win32");

    tray.setTooltip("Keychron K2 HE", "Slot 1: Default (Wired USB-C)");
    tray.setIcons(ui::renderTrayIcons(ui::Theme::blackPurple(), 0, true));
    tray.setMenu(sampleMenu());
    tray.setIcons(ui::renderTrayIcons(ui::Theme::blackPurple(), 1, false)); // repaint with another state
    std::this_thread::sleep_for(std::chrono::milliseconds(150)); // let the tray thread apply the updates

    tray.stop();
    EXPECT_FALSE(tray.isRunning());
}

TEST(Win32TrayIconTest, CanBeRestartedAfterStop) {
    ui::Win32TrayIcon tray;
    ASSERT_TRUE(tray.start());
    tray.stop();

    ASSERT_TRUE(tray.start());
    EXPECT_TRUE(tray.isRunning());
    tray.stop();
}

TEST(Win32TrayIconTest, StateSetBeforeStartIsAcceptedAndStartIsIdempotent) {
    ui::Win32TrayIcon tray;
    tray.setTooltip("Title", "Description");
    tray.setMenu(sampleMenu());

    ASSERT_TRUE(tray.start());
    EXPECT_TRUE(tray.start()) << "starting twice must not create a second window";
    tray.stop();
    tray.stop(); // and stopping twice is fine
}

TEST(Win32TrayIconTest, DestructorStopsTheThread) {
    {
        ui::Win32TrayIcon tray;
        ASSERT_TRUE(tray.start());
    } // leaving the scope must join the message-loop thread
    SUCCEED();
}

#else

TEST(Win32TrayIconTest, WindowsOnly) {
    GTEST_SKIP() << "Windows tray backend";
}

#endif
