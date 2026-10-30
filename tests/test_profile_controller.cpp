#include <gtest/gtest.h>

#include "support/FakeProfileGateway.h"
#include "ui/AppModel.h"
#include "ui/ProfileController.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <thread>

using namespace std::chrono_literals;
using ui::ActuationSettings;
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

    class ProfileControllerTest : public ::testing::Test {
    protected:
        void SetUp() override {
            gateway.slots[0] = {true, "Default", "default.json", ActuationSettings{}};
            gateway.slots[1] = {true, "Tekken 8", "tekken.json", ActuationSettings{true, 3, 1, 1}};
            gateway.slots[2] = {false, "Slot 3", "", std::nullopt};
            gateway.hardware = ActuationSettings{};
            gateway.activeSlot = 0;
        }

        void TearDown() override { controller.stop(); }

        // Waits for a command to finish: not busy any more and the last status has the given severity.
        bool waitForStatus(const StatusSeverity severity) {
            return waitUntil([&] {
                const auto state = model.snapshot();
                return !state.busy && state.status.severity == severity;
            });
        }

        test::FakeProfileGateway gateway;
        ui::AppModel             model;
        ui::ProfileController    controller{gateway, model, 20ms};
    };

} // namespace

// ---------------------------------------------------------------------------
// Start-up and state publication
// ---------------------------------------------------------------------------

TEST_F(ProfileControllerTest, StartPublishesTheInitialStateFromTheGateway) {
    controller.start();

    ASSERT_TRUE(waitUntil([&] { return model.snapshot().slots[1].name == "Tekken 8"; }));
    const auto state = model.snapshot();
    EXPECT_TRUE(state.device.connected);
    EXPECT_EQ(state.device.transport, kbd::TransportMode::Wired);
    EXPECT_EQ(state.activeSlot, 0);
    EXPECT_TRUE(state.slots[0].hasProfile);
    EXPECT_FALSE(state.slots[2].hasProfile);
    EXPECT_FALSE(state.busy);
}

TEST_F(ProfileControllerTest, CommandsQueuedBeforeStartRunAfterwards) {
    controller.activateSlot(1);
    EXPECT_EQ(gateway.countOf("activate"), 0);

    controller.start();

    ASSERT_TRUE(waitForStatus(StatusSeverity::Success));
    EXPECT_EQ(model.snapshot().activeSlot, 1);
}

TEST_F(ProfileControllerTest, CommandsAfterStopAreIgnoredAndStopIsIdempotent) {
    controller.start();
    controller.stop();
    EXPECT_NO_THROW(controller.stop());

    controller.activateSlot(1);
    std::this_thread::sleep_for(60ms);

    EXPECT_EQ(gateway.countOf("activate"), 0);
}

// ---------------------------------------------------------------------------
// Slot switching
// ---------------------------------------------------------------------------

TEST_F(ProfileControllerTest, ActivateSlotSwitchesAndReportsSuccess) {
    controller.start();

    controller.activateSlot(1);

    ASSERT_TRUE(waitForStatus(StatusSeverity::Success));
    const auto state = model.snapshot();
    EXPECT_EQ(state.activeSlot, 1);
    EXPECT_EQ(state.status.text, "Slot active");
    EXPECT_EQ(gateway.countOf("activate:1"), 1);
}

TEST_F(ProfileControllerTest, SlotIndexWrapsLikeTheFirmware) {
    controller.start();

    controller.activateSlot(7); // 7 % 3 == 1

    ASSERT_TRUE(waitForStatus(StatusSeverity::Success));
    EXPECT_EQ(gateway.countOf("activate:1"), 1);
}

TEST_F(ProfileControllerTest, FailedActivationIsReportedAndKeepsTheOldSlot) {
    gateway.activateResult = {false, "Keyboard did not confirm the switch"};
    controller.start();
    ASSERT_TRUE(waitUntil([&] { return model.snapshot().activeSlot == 0; }));

    controller.activateSlot(2);

    ASSERT_TRUE(waitForStatus(StatusSeverity::Error));
    EXPECT_EQ(model.snapshot().status.text, "Keyboard did not confirm the switch");
    EXPECT_EQ(model.snapshot().activeSlot, 0);
}

TEST_F(ProfileControllerTest, ExceptionFromTheGatewayBecomesAnErrorAndTheWorkerSurvives) {
    gateway.throwOnActivate = true;
    controller.start();

    controller.activateSlot(1);
    ASSERT_TRUE(waitForStatus(StatusSeverity::Error));
    EXPECT_TRUE(contains(model.snapshot().status.text, "boom"));

    gateway.throwOnActivate = false;
    controller.activateSlot(2);
    ASSERT_TRUE(waitForStatus(StatusSeverity::Success));
    EXPECT_EQ(model.snapshot().activeSlot, 2);
}

TEST_F(ProfileControllerTest, BusyFlagIsRaisedWhileTheKeyboardIsWorking) {
    std::promise<void> release;
    const auto gate = release.get_future().share();
    gateway.onActivate = [gate] { gate.wait(); };
    controller.start();

    controller.activateSlot(1);

    ASSERT_TRUE(waitUntil([&] { return model.snapshot().busy; }));
    EXPECT_EQ(model.snapshot().status.severity, StatusSeverity::Info);
    EXPECT_TRUE(contains(model.snapshot().status.text, "Switching to Slot 2"));

    release.set_value();
    ASSERT_TRUE(waitForStatus(StatusSeverity::Success));
    EXPECT_FALSE(model.snapshot().busy);
}

TEST_F(ProfileControllerTest, CommandsRunInOrderOnASingleWorkerThread) {
    controller.start();

    controller.activateSlot(1);
    controller.cycleProfile(true);
    controller.activateSlot(2);
    controller.cycleProfile(false);

    ASSERT_TRUE(waitUntil([&] { return gateway.countOf("cycle:prev") == 1 && !model.snapshot().busy; }));

    std::vector<std::string> commands;
    for (const auto& call : gateway.calls()) {
        if (call.rfind("activate", 0) == 0 || call.rfind("cycle", 0) == 0) commands.push_back(call);
    }
    EXPECT_EQ(commands, (std::vector<std::string>{"activate:1", "cycle:next", "activate:2", "cycle:prev"}));

    const auto threads = gateway.threadIds();
    ASSERT_FALSE(threads.empty());
    EXPECT_TRUE(std::all_of(threads.begin(), threads.end(), [&](const auto id) { return id == threads.front(); }));
    EXPECT_NE(threads.front(), std::this_thread::get_id()) << "hardware work must not run on the caller's thread";
}

TEST_F(ProfileControllerTest, GatewayFailuresDuringStateRefreshNeverLeaveTheModelBusy) {
    gateway.throwOnDescribeSlots = true;
    controller.start();

    controller.flashSlot(1); // flashSlotProfile succeeds, then describeSlots() throws

    ASSERT_TRUE(waitForStatus(StatusSeverity::Error));
    EXPECT_TRUE(contains(model.snapshot().status.text, "describeSlots failed"));
    EXPECT_FALSE(model.snapshot().busy);
}

// ---------------------------------------------------------------------------
// Actuation
// ---------------------------------------------------------------------------

TEST_F(ProfileControllerTest, ApplyActuationIsVerifiedByReadingTheKeyboardBack) {
    controller.start();
    const ActuationSettings wanted{true, 3, 1, 1};

    controller.applyActuation(1, wanted, false);

    ASSERT_TRUE(waitForStatus(StatusSeverity::Success));
    const auto state = model.snapshot();
    EXPECT_TRUE(contains(state.status.text, "verified"));
    EXPECT_EQ(state.actuation[1], wanted);
    EXPECT_EQ(gateway.countOf("apply:1:3:rt:1:1"), 1);
    EXPECT_EQ(gateway.countOf("read:1"), 1);
    EXPECT_EQ(gateway.countOf("save"), 0);
}

TEST_F(ProfileControllerTest, ApplyActuationWarnsWhenTheKeyboardReportsSomethingElse) {
    gateway.firmwareOverride = ActuationSettings{false, 3, 1, 1}; // keyboard ends up in regular mode
    controller.start();

    controller.applyActuation(1, ActuationSettings{true, 3, 1, 1}, false);

    ASSERT_TRUE(waitForStatus(StatusSeverity::Warning));
    const auto state = model.snapshot();
    EXPECT_TRUE(contains(state.status.text, "instead of"));
    EXPECT_TRUE(contains(state.status.text, "Rapid Trigger mode byte"));
    EXPECT_EQ(state.actuation[1], *gateway.firmwareOverride) << "the model must show what the keyboard really holds";
}

TEST_F(ProfileControllerTest, ApplyActuationClampsValuesBeforeTheyReachTheKeyboard) {
    controller.start();

    controller.applyActuation(1, ActuationSettings{false, 0, 99, 99}, false);

    ASSERT_TRUE(waitForStatus(StatusSeverity::Success));
    EXPECT_EQ(gateway.countOf("apply:1:1:static:39:39"), 1);
}

TEST_F(ProfileControllerTest, ApplyActuationCanSaveToFlash) {
    controller.start();

    controller.applyActuation(2, ActuationSettings{false, 14, 4, 4}, true);

    ASSERT_TRUE(waitForStatus(StatusSeverity::Success));
    EXPECT_EQ(gateway.countOf("save:2"), 1);
    EXPECT_TRUE(contains(model.snapshot().status.text, "Saved to the keyboard's flash"));
}

TEST_F(ProfileControllerTest, FailedSaveDowngradesTheOutcomeToAWarning) {
    gateway.saveResult = {false, "Keyboard rejected the save"};
    controller.start();

    controller.applyActuation(2, ActuationSettings{false, 14, 4, 4}, true);

    ASSERT_TRUE(waitForStatus(StatusSeverity::Warning));
    EXPECT_TRUE(contains(model.snapshot().status.text, "Saving to flash failed: Keyboard rejected the save"));
}

TEST_F(ProfileControllerTest, RejectedActuationSkipsVerificationAndSaving) {
    gateway.applyResult = {false, "Keyboard rejected the actuation settings"};
    controller.start();

    controller.applyActuation(1, ActuationSettings{true, 3, 1, 1}, true);

    ASSERT_TRUE(waitForStatus(StatusSeverity::Error));
    EXPECT_EQ(model.snapshot().status.text, "Keyboard rejected the actuation settings");
    EXPECT_EQ(gateway.countOf("read"), 0);
    EXPECT_EQ(gateway.countOf("save"), 0);
}

TEST_F(ProfileControllerTest, ApplyActuationWithoutReadBackIsReportedAsUnverified) {
    gateway.readSupported = false;
    controller.start();

    controller.applyActuation(1, ActuationSettings{true, 3, 1, 1}, false);

    ASSERT_TRUE(waitForStatus(StatusSeverity::Info));
    EXPECT_TRUE(contains(model.snapshot().status.text, "could not read back"));
    EXPECT_EQ(model.snapshot().actuation[1], (ActuationSettings{true, 3, 1, 1}));
}

TEST_F(ProfileControllerTest, ReadActuationPublishesWhatTheKeyboardHolds) {
    gateway.hardware = ActuationSettings{true, 5, 2, 3};
    controller.start();

    controller.readActuation(0);

    ASSERT_TRUE(waitForStatus(StatusSeverity::Info));
    EXPECT_EQ(model.snapshot().actuation[0], (ActuationSettings{true, 5, 2, 3}));
    EXPECT_TRUE(contains(model.snapshot().status.text, "0.5 mm"));
}

TEST_F(ProfileControllerTest, ReadActuationFailureIsAWarning) {
    gateway.readSupported = false;
    controller.start();

    controller.readActuation(0);

    ASSERT_TRUE(waitForStatus(StatusSeverity::Warning));
    EXPECT_FALSE(model.snapshot().actuation[0].has_value());
}

// ---------------------------------------------------------------------------
// Flashing a profile
// ---------------------------------------------------------------------------

TEST_F(ProfileControllerTest, FlashVerifiesAgainstTheProfileFile) {
    gateway.hardware = gateway.slots[1].storedSettings; // keyboard ends up with the file's values
    controller.start();

    controller.flashSlot(1);

    ASSERT_TRUE(waitForStatus(StatusSeverity::Success));
    EXPECT_EQ(gateway.countOf("flash:1"), 1);
    EXPECT_TRUE(contains(model.snapshot().status.text, "Verified"));
}

TEST_F(ProfileControllerTest, FlashWarnsWhenTheKeyboardStillHoldsOtherValues) {
    gateway.hardware = ActuationSettings{}; // the write did not take effect
    controller.start();

    controller.flashSlot(1);

    ASSERT_TRUE(waitForStatus(StatusSeverity::Warning));
    const auto text = model.snapshot().status.text;
    EXPECT_TRUE(contains(text, "while the profile file says"));
    EXPECT_TRUE(contains(text, "may not accept raw profile writes"));
}

TEST_F(ProfileControllerTest, FlashFailureIsAnError) {
    gateway.flashResult = {false, "No profile file is assigned to Slot 3"};
    controller.start();

    controller.flashSlot(2);

    ASSERT_TRUE(waitForStatus(StatusSeverity::Error));
    EXPECT_EQ(model.snapshot().status.text, "No profile file is assigned to Slot 3");
    EXPECT_EQ(gateway.countOf("read"), 0);
}

// ---------------------------------------------------------------------------
// Watcher events (the controller is an Files::IFileObserver / kbd::IDeviceObserver)
// ---------------------------------------------------------------------------

TEST_F(ProfileControllerTest, FileEventsAreForwardedAndRefreshTheSlots) {
    controller.start();
    ASSERT_TRUE(waitUntil([&] { return model.snapshot().slots[0].hasProfile; }));
    gateway.slots[2] = {true, "Gamepad", "gamepad.json", std::nullopt};

    controller.onFileCreated("/profiles/gamepad.json");

    ASSERT_TRUE(waitUntil([&] { return model.snapshot().slots[2].hasProfile; }));
    EXPECT_EQ(gateway.countOf("fileCreated:gamepad.json"), 1);
    EXPECT_EQ(model.snapshot().status.text, "Profile file added: gamepad.json");

    gateway.slots[2] = {false, "Slot 3", "", std::nullopt};
    controller.onFileDeleted("/profiles/gamepad.json");

    ASSERT_TRUE(waitUntil([&] { return !model.snapshot().slots[2].hasProfile; }));
    EXPECT_EQ(gateway.countOf("fileDeleted:gamepad.json"), 1);
}

TEST_F(ProfileControllerTest, DeviceConnectedUpdatesTransportAndReappliesViaTheGateway) {
    gateway.setDevice({false, kbd::TransportMode::Disconnected, ""});
    controller.start();
    ASSERT_TRUE(waitUntil([&] { return model.snapshot().status.serial == 0 && !model.snapshot().device.connected; }));

    gateway.setDevice({true, kbd::TransportMode::WirelessDongle, "dongle-path"});
    controller.onDeviceConnected(kbd::TransportMode::WirelessDongle, "dongle-path");

    ASSERT_TRUE(waitForStatus(StatusSeverity::Success));
    const auto state = model.snapshot();
    EXPECT_TRUE(state.device.connected);
    EXPECT_EQ(state.device.transport, kbd::TransportMode::WirelessDongle);
    EXPECT_EQ(state.device.path, "dongle-path");
    EXPECT_EQ(gateway.countOf("deviceConnected:2.4G dongle:dongle-path"), 1);
    EXPECT_TRUE(contains(state.status.text, "2.4G dongle"));
}

TEST_F(ProfileControllerTest, DeviceDisconnectedClearsDeviceAndActiveSlot) {
    controller.start();
    ASSERT_TRUE(waitUntil([&] { return model.snapshot().activeSlot == 0; }));

    gateway.setDevice({false, kbd::TransportMode::Disconnected, ""});
    controller.onDeviceDisconnected();

    ASSERT_TRUE(waitForStatus(StatusSeverity::Warning));
    const auto state = model.snapshot();
    EXPECT_FALSE(state.device.connected);
    EXPECT_EQ(state.activeSlot, -1);
    EXPECT_EQ(gateway.countOf("deviceDisconnected"), 1);
}

// ---------------------------------------------------------------------------
// Polling
// ---------------------------------------------------------------------------

TEST_F(ProfileControllerTest, PollingPicksUpSlotChangesMadeOnTheKeyboardItself) {
    controller.start();
    ASSERT_TRUE(waitUntil([&] { return model.snapshot().activeSlot == 0; }));

    controller.setPollingEnabled(true);
    gateway.setActiveSlot(2); // e.g. the user pressed Fn+P+3 on the keyboard

    EXPECT_TRUE(waitUntil([&] { return model.snapshot().activeSlot == 2; }));
}

TEST_F(ProfileControllerTest, NoPollingTrafficWhileDisabled) {
    controller.start();
    ASSERT_TRUE(waitUntil([&] { return model.snapshot().activeSlot == 0; }));
    std::this_thread::sleep_for(80ms); // let any in-flight work settle
    const int queriesBefore = gateway.countOf("query");

    std::this_thread::sleep_for(200ms); // ten poll intervals

    EXPECT_EQ(gateway.countOf("query"), queriesBefore);
}
