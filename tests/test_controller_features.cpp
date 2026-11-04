#include <gtest/gtest.h>

#include "support/FakeProfileGateway.h"
#include "ui/AppModel.h"
#include "ui/DemoProfileGateway.h"
#include "ui/ProfileController.h"

#include <chrono>
#include <thread>

using namespace std::chrono_literals;
using ui::AdvancedKeyChange;
using ui::StatusSeverity;

namespace {

    template <typename Predicate>
    bool waitUntil(Predicate&& predicate, const std::chrono::milliseconds timeout = 3000ms) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (predicate()) return true;
            std::this_thread::sleep_for(1ms);
        }
        return predicate();
    }

    bool contains(const std::string& text, const std::string& part) {
        return text.find(part) != std::string::npos;
    }

    class ControllerFeaturesTest : public ::testing::Test {
    protected:
        void SetUp() override {
            gateway.slots[0] = {true, "Default", "default.json", ui::ActuationSettings{}, std::nullopt};
            controller.start();
            ASSERT_TRUE(waitUntil([&] { return gateway.countOf("describeDevice") > 0 || model.snapshot().device.connected; }));
        }
        void TearDown() override { controller.stop(); }

        // Waits until a command has finished with the given severity (its serial is newer than `after`).
        bool finished(const StatusSeverity severity, const std::uint64_t after) {
            return waitUntil([&] {
                const auto state = model.snapshot();
                return !state.busy && state.status.serial > after && state.status.severity == severity;
            });
        }

        test::FakeProfileGateway gateway;
        ui::AppModel             model;
        ui::ProfileController    controller{gateway, model, 20ms};
    };

    AdvancedKeyChange gamepad(const ui::KeyPosition key, const std::uint8_t target) {
        AdvancedKeyChange change;
        change.key   = key;
        change.kind  = AdvancedKeyChange::Kind::Gamepad;
        change.index = target;
        return change;
    }

} // namespace

TEST_F(ControllerFeaturesTest, AdvancedKeysAreSentReadBackAndSaved) {
    const auto before = model.snapshot().status.serial;
    controller.applyAdvancedKeys(0, {gamepad({2, 2}, kbd::GamepadTarget::LEFT_STICK_UP), gamepad({3, 1}, kbd::GamepadTarget::LEFT_STICK_LEFT)}, true);

    ASSERT_TRUE(finished(StatusSeverity::Success, before)) << model.snapshot().status.text;
    EXPECT_EQ(gateway.countOf("advanced:0:"), 2);
    EXPECT_EQ(gateway.countOf("save:0"), 1);
    const auto state = model.snapshot();
    ASSERT_TRUE(state.details[0]);
    EXPECT_EQ(state.details[0]->at({2, 2}), (ui::KeyAdvanced{kbd::AdvancedMode::GAMEPAD, kbd::GamepadTarget::LEFT_STICK_UP}));
    EXPECT_TRUE(contains(state.status.text, "Verified"));
}

TEST_F(ControllerFeaturesTest, AModeTheKeyboardDidNotStoreIsAWarning) {
    gateway.keyModeOverride = kbd::AdvancedMode::NONE;
    const auto before = model.snapshot().status.serial;
    controller.applyAdvancedKeys(0, {gamepad({2, 2}, kbd::GamepadTarget::LEFT_STICK_UP)}, false);
    ASSERT_TRUE(finished(StatusSeverity::Warning, before));
    EXPECT_TRUE(contains(model.snapshot().status.text, "1 of 1"));
    EXPECT_EQ(gateway.countOf("save:"), 0);
}

TEST_F(ControllerFeaturesTest, WithoutReadBackTheExpectedModesAreShown) {
    gateway.detailsSupported = false;
    const auto before = model.snapshot().status.serial;
    AdvancedKeyChange sprint;
    sprint.key   = {2, 2};
    sprint.kind  = AdvancedKeyChange::Kind::DynamicKeystroke;
    sprint.index = 3;
    sprint.okmc  = kbd::deepPressCombo(kbd::Keycode::W, kbd::Keycode::LEFT_SHIFT, 10, 30);
    controller.applyAdvancedKeys(1, {sprint}, false);
    ASSERT_TRUE(finished(StatusSeverity::Info, before));
    const auto details = model.snapshot().details[1];
    ASSERT_TRUE(details);
    EXPECT_EQ(details->at({2, 2}), (ui::KeyAdvanced{kbd::AdvancedMode::DKS, 3}));
    EXPECT_EQ(details->okmc[3], sprint.okmc);
}

TEST_F(ControllerFeaturesTest, SocdWritesAllTwentyEntriesAndDropsInvalidPairs) {
    const auto before = model.snapshot().status.serial;
    controller.applySocdPairs(0, {kbd::SocdPair{3, 1, 3, 3, 3}, kbd::SocdPair{2, 2, 2, 2, 6}, kbd::SocdPair{2, 2, 3, 2, 6}}, false);
    ASSERT_TRUE(finished(StatusSeverity::Success, before)) << model.snapshot().status.text;
    EXPECT_EQ(gateway.countOf("socd:0:"), 20);
    EXPECT_EQ(gateway.countOf("socd:0:0:3"), 1);
    EXPECT_EQ(gateway.countOf("socd:0:1:6"), 1);
    EXPECT_EQ(gateway.countOf("socd:0:2:0"), 1) << "the same-key pair was dropped, so entry 2 is cleared";
    const auto details = model.snapshot().details[0];
    ASSERT_TRUE(details);
    EXPECT_EQ(details->socdPairs().size(), 2u);
}

TEST_F(ControllerFeaturesTest, ControllerSettingsAreReadAndWritten) {
    controller.readControllerSettings();
    ASSERT_TRUE(waitUntil([&] { return model.snapshot().controller.curve.has_value(); }));
    EXPECT_EQ(*model.snapshot().controller.mode, kbd::ControllerMode::XINPUT);

    auto before = model.snapshot().status.serial;
    controller.applyControllerMode(kbd::ControllerMode::XINPUT | kbd::ControllerMode::TYPING | 0x80);
    ASSERT_TRUE(finished(StatusSeverity::Success, before));
    EXPECT_EQ(gateway.countOf("controllerMode:3"), 1) << "unknown bits are dropped";
    // Only the typing bit changed: the firmware applies that at once, so no replug is asked for.
    EXPECT_TRUE(contains(model.snapshot().status.text, "already running"));
    EXPECT_FALSE(model.snapshot().controller.replugNeeded);

    before = model.snapshot().status.serial;
    controller.applyCurve({{{0, 0}, {20, 31}, {10, 95}, {40, 127}}});
    ASSERT_TRUE(finished(StatusSeverity::Error, before));
    EXPECT_EQ(gateway.countOf("curve:"), 0) << "an unordered curve never reaches the keyboard";

    before = model.snapshot().status.serial;
    const kbd::ResponseCurve precise{{{2, 0}, {16, 20}, {32, 70}, {38, 127}}};
    controller.applyCurve(precise);
    ASSERT_TRUE(finished(StatusSeverity::Success, before));
    EXPECT_EQ(model.snapshot().controller.curve, precise);
}

TEST_F(ControllerFeaturesTest, LiveTravelIsPolledOnlyWhileTheWindowIsOpenAndKeysAreWatched) {
    gateway.liveReadings = {ui::LiveKeyTravel{{2, 2}, 27, 162, true}};
    controller.watchLiveTravel({{2, 2}});
    std::this_thread::sleep_for(80ms);
    EXPECT_EQ(gateway.countOf("live:"), 0) << "no window: no polling";

    controller.setPollingEnabled(true);
    ASSERT_TRUE(waitUntil([&] { return !controller.liveTravel().empty(); }));
    EXPECT_EQ(controller.liveTravel().front().travelTenths, 27);
    ASSERT_TRUE(waitUntil([&] { return gateway.countOf("live:2,2") >= 3; })) << "polled repeatedly";

    controller.watchLiveTravel({});
    EXPECT_TRUE(controller.liveTravel().empty());
    std::this_thread::sleep_for(40ms);
    const int calls = gateway.countOf("live:");
    std::this_thread::sleep_for(80ms);
    EXPECT_EQ(gateway.countOf("live:"), calls) << "stopped";
}

TEST(DemoGatewayFeaturesTest, SlotsCarryAdvancedKeysThatCanBeEdited) {
    ui::DemoProfileGateway gateway(0ms, 2);
    EXPECT_EQ(gateway.queryActiveSlot(), 2);
    const auto tekken = gateway.readSlotDetails(1);
    ASSERT_TRUE(tekken);
    EXPECT_EQ(tekken->socdPairs().size(), 3u);

    const auto analog = gateway.readSlotDetails(2);
    ASSERT_TRUE(analog);
    EXPECT_EQ(analog->at({2, 2}).mode, kbd::AdvancedMode::GAMEPAD);

    AdvancedKeyChange clear;
    clear.key = {2, 2};
    ASSERT_TRUE(gateway.applyAdvancedKey(2, clear).ok);
    EXPECT_EQ(gateway.readSlotDetails(2)->at({2, 2}).mode, kbd::AdvancedMode::NONE);
    EXPECT_FALSE(gateway.applyCurve({{{0, 0}, {20, 31}, {10, 95}, {40, 127}}}).ok);

    const auto live = gateway.readLiveTravel({2, 2});
    ASSERT_TRUE(live);
    EXPECT_LE(live->travelTenths, 40);
}
