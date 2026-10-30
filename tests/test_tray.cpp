#include <gtest/gtest.h>

#include "support/Fakes.h"
#include "ui/AppModel.h"
#include "ui/ThemeManager.h"
#include "ui/TrayIconRenderer.h"
#include "ui/TrayMenuBuilder.h"
#include "ui/TrayPresenter.h"

#include <algorithm>
#include <set>

using ui::TrayAction;
using ui::TrayMenuItem;

namespace {

    ui::AppSnapshot connectedState(const int activeSlot = 1) {
        ui::AppSnapshot state;
        state.device = {true, kbd::TransportMode::Wired, "path"};
        state.activeSlot = activeSlot;
        state.slots[0] = {true, "Default", "default.json", std::nullopt};
        state.slots[1] = {true, "Tekken 8", "tekken.json", std::nullopt};
        state.slots[2] = {false, "Slot 3", "", std::nullopt};
        return state;
    }

    const TrayMenuItem* findAction(const ui::TrayMenu& menu, const TrayAction action) {
        const auto it = std::find_if(menu.items.begin(), menu.items.end(),
                                     [action](const TrayMenuItem& item) { return item.action == action; });
        return it == menu.items.end() ? nullptr : &*it;
    }

    std::vector<std::string> headers(const ui::TrayMenu& menu) {
        std::vector<std::string> result;
        for (const auto& item : menu.items) {
            if (item.kind == TrayMenuItem::Kind::Header) result.push_back(item.label);
        }
        return result;
    }

} // namespace

// ---------------------------------------------------------------------------
// Menu model
// ---------------------------------------------------------------------------

TEST(TrayMenuBuilderTest, ConnectedMenuListsSlotsWithTheActiveOneSelected) {
    const auto menu = ui::tray::buildMenu(connectedState(1), false);

    EXPECT_EQ(headers(menu), (std::vector<std::string>{"Keychron K2 HE - Wired USB-C", "Active: Slot 2 - Tekken 8"}));

    const TrayMenuItem* slot1 = findAction(menu, TrayAction::ActivateSlot1);
    const TrayMenuItem* slot2 = findAction(menu, TrayAction::ActivateSlot2);
    const TrayMenuItem* slot3 = findAction(menu, TrayAction::ActivateSlot3);
    ASSERT_TRUE(slot1 && slot2 && slot3);
    EXPECT_EQ(slot1->label, "Slot 1 - Default");
    EXPECT_EQ(slot3->label, "Slot 3 - no profile");
    for (const auto* slot : {slot1, slot2, slot3}) {
        EXPECT_EQ(slot->kind, TrayMenuItem::Kind::Radio);
        EXPECT_TRUE(slot->enabled);
    }
    EXPECT_FALSE(slot1->checked);
    EXPECT_TRUE(slot2->checked);
    EXPECT_FALSE(slot3->checked);
}

TEST(TrayMenuBuilderTest, MenuContainsEveryApplicationAction) {
    const auto menu = ui::tray::buildMenu(connectedState(), false);

    for (const auto action : {TrayAction::OpenDashboard, TrayAction::NextProfile, TrayAction::PreviousProfile,
                              TrayAction::OpenProfilesFolder, TrayAction::OpenConfigFile, TrayAction::OpenLogFile,
                              TrayAction::ToggleAutostart, TrayAction::Quit}) {
        EXPECT_NE(findAction(menu, action), nullptr) << "missing action " << static_cast<int>(action);
    }
    EXPECT_EQ(menu.items.back().action, TrayAction::Quit) << "Exit belongs at the bottom";
}

TEST(TrayMenuBuilderTest, ItemIdsAreUniquePositiveAndStable) {
    const auto menu = ui::tray::buildMenu(connectedState(), true);

    std::set<int> ids;
    for (const auto& item : menu.items) {
        EXPECT_GT(item.id, 0);
        EXPECT_TRUE(ids.insert(item.id).second) << "duplicate id " << item.id;
        if (item.action != TrayAction::None) {
            EXPECT_EQ(item.id, ui::tray::idFor(item.action));
        }
    }

    // The same action keeps its id even when the rest of the menu changes.
    const auto other = ui::tray::buildMenu(ui::AppSnapshot{}, false);
    EXPECT_EQ(findAction(other, TrayAction::Quit)->id, findAction(menu, TrayAction::Quit)->id);
}

TEST(TrayMenuBuilderTest, DisconnectedKeyboardDisablesSwitchingButKeepsTheRest) {
    const auto menu = ui::tray::buildMenu(ui::AppSnapshot{}, false);

    EXPECT_EQ(headers(menu), (std::vector<std::string>{"Keychron K2 HE - disconnected"}));
    for (const auto action : {TrayAction::ActivateSlot1, TrayAction::ActivateSlot2, TrayAction::ActivateSlot3,
                              TrayAction::NextProfile, TrayAction::PreviousProfile}) {
        EXPECT_FALSE(findAction(menu, action)->enabled) << static_cast<int>(action);
    }
    for (const auto action : {TrayAction::OpenDashboard, TrayAction::OpenProfilesFolder, TrayAction::OpenConfigFile,
                              TrayAction::OpenLogFile, TrayAction::ToggleAutostart, TrayAction::Quit}) {
        EXPECT_TRUE(findAction(menu, action)->enabled) << static_cast<int>(action);
    }
}

TEST(TrayMenuBuilderTest, BusyKeyboardTemporarilyDisablesSwitching) {
    auto state = connectedState();
    state.busy = true;

    const auto menu = ui::tray::buildMenu(state, false);

    EXPECT_FALSE(findAction(menu, TrayAction::ActivateSlot1)->enabled);
    EXPECT_FALSE(findAction(menu, TrayAction::NextProfile)->enabled);
    EXPECT_TRUE(findAction(menu, TrayAction::Quit)->enabled);
}

TEST(TrayMenuBuilderTest, CyclingNeedsAtLeastOneProfileButSlotsStayAvailable) {
    auto state = connectedState();
    for (auto& slot : state.slots) slot.hasProfile = false;

    const auto menu = ui::tray::buildMenu(state, false);

    EXPECT_FALSE(findAction(menu, TrayAction::NextProfile)->enabled);
    EXPECT_FALSE(findAction(menu, TrayAction::PreviousProfile)->enabled);
    EXPECT_TRUE(findAction(menu, TrayAction::ActivateSlot2)->enabled);
}

TEST(TrayMenuBuilderTest, AutostartCheckMarkFollowsTheFlag) {
    const auto off = ui::tray::buildMenu(connectedState(), false);
    const auto on  = ui::tray::buildMenu(connectedState(), true);

    EXPECT_EQ(findAction(off, TrayAction::ToggleAutostart)->kind, TrayMenuItem::Kind::Check);
    EXPECT_FALSE(findAction(off, TrayAction::ToggleAutostart)->checked);
    EXPECT_TRUE(findAction(on, TrayAction::ToggleAutostart)->checked);
}

TEST(TrayMenuBuilderTest, SameStateGivesTheSameMenu) {
    EXPECT_EQ(ui::tray::buildMenu(connectedState(1), false), ui::tray::buildMenu(connectedState(1), false));
    EXPECT_NE(ui::tray::buildMenu(connectedState(1), false), ui::tray::buildMenu(connectedState(2), false));
}

TEST(TrayMenuBuilderTest, FindLooksItemsUpById) {
    const auto menu = ui::tray::buildMenu(connectedState(), false);

    ASSERT_NE(menu.find(ui::tray::idFor(TrayAction::Quit)), nullptr);
    EXPECT_EQ(menu.find(ui::tray::idFor(TrayAction::Quit))->label, "Exit");
    EXPECT_EQ(menu.find(424242), nullptr);
}

TEST(TrayMenuBuilderTest, TooltipDescribesTheActiveProfile) {
    EXPECT_EQ(ui::tray::tooltipTitle(), "Keychron K2 HE");
    EXPECT_EQ(ui::tray::tooltipDescription(ui::AppSnapshot{}), "Not connected");
    EXPECT_EQ(ui::tray::tooltipDescription(connectedState(-1)), "Connected (Wired USB-C)");
    EXPECT_EQ(ui::tray::tooltipDescription(connectedState(1)), "Slot 2: Tekken 8 (Wired USB-C)");
    EXPECT_EQ(ui::tray::tooltipDescription(connectedState(2)), "Slot 3 (Wired USB-C)");
}

// ---------------------------------------------------------------------------
// Icon rendering
// ---------------------------------------------------------------------------

namespace {

    struct Pixel {
        ui::Color color;
        int       alpha;
    };

    Pixel pixelAt(const ui::TrayImage& image, const int x, const int y) {
        const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) * 4;
        return {ui::Color(image.rgba[i], image.rgba[i + 1], image.rgba[i + 2]), image.rgba[i + 3]};
    }

}

TEST(TrayIconRendererTest, ProducesWellFormedImagesAtEverySize) {
    const auto images = ui::renderTrayIcons(ui::Theme::blackPurple(), 0, true);

    ASSERT_EQ(images.size(), ui::TRAY_ICON_SIZES.size());
    for (std::size_t i = 0; i < images.size(); ++i) {
        EXPECT_EQ(images[i].width, ui::TRAY_ICON_SIZES[i]);
        EXPECT_EQ(images[i].height, ui::TRAY_ICON_SIZES[i]);
        EXPECT_EQ(images[i].rgba.size(), static_cast<std::size_t>(images[i].width * images[i].height * 4));
    }
}

TEST(TrayIconRendererTest, NonPositiveSizeGivesAnEmptyImage) {
    EXPECT_TRUE(ui::renderTrayIcon(0, ui::Theme::blackPurple(), 0, true).rgba.empty());
    EXPECT_TRUE(ui::renderTrayIcon(-4, ui::Theme::blackPurple(), 0, true).rgba.empty());
}

TEST(TrayIconRendererTest, CornersAreTransparentAndTheRimIsThePurpleAccent) {
    const ui::Theme theme = ui::Theme::blackPurple();
    const auto icon = ui::renderTrayIcon(64, theme, 0, true);

    EXPECT_EQ(pixelAt(icon, 0, 0).alpha, 0) << "rounded corner";
    EXPECT_EQ(pixelAt(icon, 63, 63).alpha, 0);

    const Pixel rim = pixelAt(icon, 32, 0); // middle of the top edge
    EXPECT_EQ(rim.alpha, 255);
    EXPECT_EQ(rim.color, theme.palette.accent);

    const Pixel plate = pixelAt(icon, 32, 12); // inside the rim, away from the dots: the black plate
    EXPECT_EQ(plate.alpha, 255);
    EXPECT_EQ(plate.color, theme.palette.background);
}

TEST(TrayIconRendererTest, OnlyTheActiveSlotsDotIsLit) {
    const ui::Theme theme = ui::Theme::blackPurple();
    // Dot centres sit at x = 0.5 + {-0.26, 0, +0.26} of the width, on the middle row.
    const int leftX = 15, midX = 32, rightX = 48, y = 32;

    for (int active = 0; active < 3; ++active) {
        const auto icon = ui::renderTrayIcon(64, theme, active, true);
        const int xs[] = {leftX, midX, rightX};
        for (int slot = 0; slot < 3; ++slot) {
            const bool lit = pixelAt(icon, xs[slot], y).color == theme.palette.accentText;
            EXPECT_EQ(lit, slot == active) << "active=" << active << " slot=" << slot;
        }
    }
}

TEST(TrayIconRendererTest, DisconnectedKeyboardGreysEverythingOut) {
    const ui::Theme theme = ui::Theme::blackPurple();
    const auto icon = ui::renderTrayIcon(64, theme, 1, false);

    const auto left = pixelAt(icon, 15, 32).color;
    EXPECT_EQ(pixelAt(icon, 32, 32).color, left);
    EXPECT_EQ(pixelAt(icon, 48, 32).color, left);
    EXPECT_NE(left, theme.palette.accentText);
    EXPECT_EQ(pixelAt(icon, 32, 0).color, theme.palette.border) << "the rim loses its purple";
}

TEST(TrayIconRendererTest, IconFollowsTheTheme) {
    ui::Theme custom = ui::Theme::blackPurple();
    custom.palette.accent = ui::Color(255, 0, 0);
    custom.palette.accentText = ui::Color(0, 255, 0);

    const auto icon = ui::renderTrayIcon(64, custom, 1, true);

    EXPECT_EQ(pixelAt(icon, 32, 0).color, ui::Color(255, 0, 0));
    EXPECT_EQ(pixelAt(icon, 32, 32).color, ui::Color(0, 255, 0));
}

TEST(TrayIconRendererTest, RenderingIsDeterministic) {
    const ui::Theme theme = ui::Theme::blackPurple();
    EXPECT_EQ(ui::renderTrayIcon(32, theme, 2, true), ui::renderTrayIcon(32, theme, 2, true));
    EXPECT_NE(ui::renderTrayIcon(32, theme, 2, true), ui::renderTrayIcon(32, theme, 1, true));
}

TEST(TrayIconRendererTest, ScalingKeepsColoursAndTransparency) {
    const ui::Theme theme = ui::Theme::blackPurple();
    const auto big = ui::renderTrayIcon(64, theme, 1, true);

    const auto half = ui::scaleTrayImage(big, 32);

    ASSERT_EQ(half.width, 32);
    ASSERT_EQ(half.rgba.size(), 32u * 32u * 4u);
    EXPECT_EQ(pixelAt(half, 0, 0).alpha, 0);
    EXPECT_EQ(pixelAt(half, 16, 0).color, theme.palette.accent) << "uniform areas must keep their exact colour";
    EXPECT_EQ(pixelAt(half, 16, 16).color, theme.palette.accentText);

    EXPECT_TRUE(ui::scaleTrayImage(big, 0).rgba.empty());
    EXPECT_TRUE(ui::scaleTrayImage(ui::TrayImage{}, 16).rgba.empty());
    EXPECT_EQ(ui::scaleTrayImage(big, 64), big) << "same size is an identity";
}

// ---------------------------------------------------------------------------
// TrayIconBase
// ---------------------------------------------------------------------------

TEST(TrayIconBaseTest, OnlyRealChangesReachTheBackend) {
    test::FakeTrayIcon tray;
    const auto menu = ui::tray::buildMenu(connectedState(), false);

    tray.setMenu(menu);
    tray.setMenu(menu); // identical
    tray.setTooltip("Title", "Text");
    tray.setTooltip("Title", "Text"); // identical
    tray.setIcons(ui::renderTrayIcons(ui::Theme::blackPurple(), 0, true));
    tray.setIcons(ui::renderTrayIcons(ui::Theme::blackPurple(), 0, true)); // identical

    EXPECT_EQ(tray.menuUpdates, 1);
    EXPECT_EQ(tray.tooltipUpdates, 1);
    EXPECT_EQ(tray.iconUpdates, 1);
    EXPECT_EQ(tray.menu(), menu);
    EXPECT_EQ(tray.title(), "Title");
    EXPECT_EQ(tray.description(), "Text");

    tray.setTooltip("Title", "Other text");
    EXPECT_EQ(tray.tooltipUpdates, 2);
}

TEST(TrayIconBaseTest, EventsReachEveryObserverUntilRemoved) {
    struct Recorder : ui::ITrayObserver {
        void onTrayActivated() override { ++activated; }
        void onTraySecondaryActivated() override { ++secondary; }
        void onTrayMenuItemSelected(const int id) override { chosen.push_back(id); }
        int activated = 0, secondary = 0;
        std::vector<int> chosen;
    } a, b;
    test::FakeTrayIcon tray;
    tray.addObserver(&a);
    tray.addObserver(&b);

    tray.click();
    tray.middleClick();
    tray.choose(7);
    tray.removeObserver(&b);
    tray.click();

    EXPECT_EQ(a.activated, 2);
    EXPECT_EQ(a.secondary, 1);
    EXPECT_EQ(a.chosen, (std::vector<int>{7}));
    EXPECT_EQ(b.activated, 1) << "b was removed before the last click";
}

// ---------------------------------------------------------------------------
// TrayPresenter
// ---------------------------------------------------------------------------

namespace {

    class TrayPresenterTest : public ::testing::Test {
    protected:
        void SetUp() override {
            ui::ThemeManager::getInstance().resetToDefault();
            model.update([](ui::AppSnapshot& state) { state = connectedState(1); });
        }

        void TearDown() override {
            presenter.stop();
            ui::ThemeManager::getInstance().resetToDefault();
        }

        void choose(const TrayAction action) { tray.choose(ui::tray::idFor(action)); }

        ui::AppModel                model;
        test::FakeTrayIcon          tray;
        test::FakeProfileCommands   commands;
        test::FakeSystemIntegration system;
        test::FakeWindowController  window;
        int                         quitCalls = 0;
        ui::TrayPresenter           presenter{model, tray, commands, system, window, [this] { ++quitCalls; }};
    };

} // namespace

TEST_F(TrayPresenterTest, StartShowsTheCompleteTrayBeforeStartingTheBackend) {
    EXPECT_TRUE(presenter.start());

    EXPECT_TRUE(tray.isRunning());
    EXPECT_EQ(tray.menu(), ui::tray::buildMenu(connectedState(1), false));
    EXPECT_EQ(tray.title(), "Keychron K2 HE");
    EXPECT_EQ(tray.description(), "Slot 2: Tekken 8 (Wired USB-C)");
    EXPECT_EQ(tray.icons().size(), ui::TRAY_ICON_SIZES.size());
}

TEST_F(TrayPresenterTest, StartReportsWhenThereIsNoTray) {
    tray.startResult = false;

    EXPECT_FALSE(presenter.start());
    EXPECT_FALSE(tray.isRunning());
    EXPECT_FALSE(tray.menu().items.empty()) << "state is still pushed so a later host sees it";
}

TEST_F(TrayPresenterTest, ModelChangesUpdateMenuTooltipAndIcon) {
    presenter.start();
    const int iconsBefore = tray.iconUpdates;

    model.update([](ui::AppSnapshot& state) { state.activeSlot = 0; });

    EXPECT_EQ(tray.description(), "Slot 1: Default (Wired USB-C)");
    EXPECT_TRUE(tray.menu().find(ui::tray::idFor(TrayAction::ActivateSlot1))->checked);
    EXPECT_EQ(tray.iconUpdates, iconsBefore + 1) << "the lit dot moved";
}

TEST_F(TrayPresenterTest, IrrelevantChangesDoNotRedrawTheIcon) {
    presenter.start();
    const int iconsBefore = tray.iconUpdates;

    model.setStatus(ui::StatusSeverity::Info, "Something happened");
    model.update([](ui::AppSnapshot& state) { state.slots[0].name = "Renamed"; });

    EXPECT_EQ(tray.iconUpdates, iconsBefore);
    EXPECT_EQ(tray.menu().find(ui::tray::idFor(TrayAction::ActivateSlot1))->label, "Slot 1 - Renamed");
}

TEST_F(TrayPresenterTest, DisconnectingGreysTheIconAndDisablesSwitching) {
    presenter.start();
    const int iconsBefore = tray.iconUpdates;

    model.update([](ui::AppSnapshot& state) { state.device = {}; state.activeSlot = -1; });

    EXPECT_EQ(tray.description(), "Not connected");
    EXPECT_FALSE(tray.menu().find(ui::tray::idFor(TrayAction::ActivateSlot1))->enabled);
    EXPECT_EQ(tray.iconUpdates, iconsBefore + 1);
}

TEST_F(TrayPresenterTest, ThemeChangesRepaintTheIcon) {
    presenter.start();
    const int iconsBefore = tray.iconUpdates;

    ui::Theme custom = ui::Theme::blackPurple();
    custom.palette.accent = ui::Color(255, 0, 255);
    ui::ThemeManager::getInstance().setTheme(custom);

    EXPECT_EQ(tray.iconUpdates, iconsBefore + 1);
    EXPECT_EQ(pixelAt(tray.icons().back(), 32, 0).color, ui::Color(255, 0, 255));
}

TEST_F(TrayPresenterTest, LeftClickTogglesTheDashboardMiddleClickCyclesProfiles) {
    presenter.start();

    tray.click();
    tray.middleClick();

    EXPECT_EQ(window.toggles, 1);
    EXPECT_EQ(commands.calls(), (std::vector<std::string>{"cycle:next"}));
}

TEST_F(TrayPresenterTest, MenuChoicesBecomeApplicationCommands) {
    presenter.start();

    choose(TrayAction::ActivateSlot1);
    choose(TrayAction::ActivateSlot2);
    choose(TrayAction::ActivateSlot3);
    choose(TrayAction::NextProfile);
    choose(TrayAction::PreviousProfile);
    choose(TrayAction::OpenDashboard);
    choose(TrayAction::OpenProfilesFolder);
    choose(TrayAction::OpenConfigFile);
    choose(TrayAction::OpenLogFile);
    choose(TrayAction::Quit);

    EXPECT_EQ(commands.calls(), (std::vector<std::string>{"activate:0", "activate:1", "activate:2", "cycle:next", "cycle:prev"}));
    EXPECT_EQ(window.shows, 1);
    EXPECT_EQ(system.calls(), (std::vector<std::string>{"profiles", "config", "log"}));
    EXPECT_EQ(quitCalls, 1);
}

TEST_F(TrayPresenterTest, UnknownAndDisabledEntriesAreIgnored) {
    presenter.start();
    model.update([](ui::AppSnapshot& state) { state.device = {}; }); // slot switching becomes disabled

    tray.choose(999999);                       // id the menu does not contain
    choose(TrayAction::ActivateSlot1);         // disabled while disconnected
    tray.choose(200);                          // a header

    EXPECT_TRUE(commands.calls().empty());
}

TEST_F(TrayPresenterTest, TogglingAutostartFlipsTheSettingAndRefreshesTheCheckMark) {
    presenter.start();
    EXPECT_FALSE(tray.menu().find(ui::tray::idFor(TrayAction::ToggleAutostart))->checked);

    choose(TrayAction::ToggleAutostart);

    EXPECT_TRUE(system.autostart);
    EXPECT_TRUE(tray.menu().find(ui::tray::idFor(TrayAction::ToggleAutostart))->checked);

    choose(TrayAction::ToggleAutostart);

    EXPECT_FALSE(system.autostart);
    EXPECT_FALSE(tray.menu().find(ui::tray::idFor(TrayAction::ToggleAutostart))->checked);
    EXPECT_EQ(system.calls(), (std::vector<std::string>{"autostart:on", "autostart:off"}));
}

TEST_F(TrayPresenterTest, StopUnsubscribesFromEverything) {
    presenter.start();
    presenter.stop();
    const int menuUpdates = tray.menuUpdates;

    model.update([](ui::AppSnapshot& state) { state.activeSlot = 2; });
    ui::Theme custom = ui::Theme::blackPurple();
    custom.name = "changed";
    custom.palette.accent = ui::Color(1, 2, 3);
    ui::ThemeManager::getInstance().setTheme(custom);
    tray.click();

    EXPECT_EQ(tray.menuUpdates, menuUpdates);
    EXPECT_EQ(window.toggles, 0);
    EXPECT_FALSE(tray.isRunning());
}

TEST_F(TrayPresenterTest, StartIsIdempotent) {
    EXPECT_TRUE(presenter.start());
    EXPECT_TRUE(presenter.start());

    tray.click();
    EXPECT_EQ(window.toggles, 1) << "double start must not register the presenter twice";
}
