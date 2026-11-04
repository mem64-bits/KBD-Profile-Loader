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
            gateway.slots[0] = {true, "Default", "default.json", ActuationSettings{}, std::nullopt};
            gateway.slots[1] = {true, "Tekken 8", "tekken.json", ActuationSettings{true, 3, 1, 1}, std::nullopt};
            gateway.slots[2] = {false, "Slot 3", "", std::nullopt, std::nullopt};
            gateway.hardware = ActuationSettings{};
            gateway.activeSlot = 0;
            // These tests are about switching; the automatic gamepad mode has its own tests further down.
            controller.setAutoControllerMode(false);
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
    EXPECT_TRUE(contains(text, "while the file says")) << text;
}

TEST_F(ProfileControllerTest, FlashWarnsWhenKeysDifferFromTheFile) {
    ui::KeyMatrix fileKeys;
    fileKeys.at({2, 2}) = ui::KeyActuation{ui::KeyActuation::MODE_RAPID, 3, 1, 1};
    gateway.slots[1].storedKeys = fileKeys;
    gateway.hardware = gateway.slots[1].storedSettings; // global matches, but the keyboard kept no key override
    controller.start();

    controller.flashSlot(1);

    ASSERT_TRUE(waitForStatus(StatusSeverity::Warning));
    EXPECT_TRUE(contains(model.snapshot().status.text, "1 keys differ from the file")) << model.snapshot().status.text;
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
// Per-key actuation
// ---------------------------------------------------------------------------

TEST_F(ProfileControllerTest, KeyActuationIsVerifiedAndPublished) {
    controller.start();
    const ui::KeyActuation value{ui::KeyActuation::MODE_RAPID, 3, 1, 1};

    controller.applyKeyActuation(1, {{2, 2}, {3, 1}}, value, false);

    ASSERT_TRUE(waitForStatus(StatusSeverity::Success));
    EXPECT_EQ(gateway.countOf("applyKeys:1:2:2:3"), 1);
    EXPECT_EQ(gateway.countOf("readKeys:1"), 1);
    EXPECT_TRUE(contains(model.snapshot().status.text, "Verified"));
    ASSERT_TRUE(model.snapshot().keyMatrix[1].has_value());
    EXPECT_EQ(model.snapshot().keyMatrix[1]->at({3, 1}), value);
    EXPECT_TRUE(model.snapshot().keyMatrix[1]->at({3, 2}).followsGlobal());
}

TEST_F(ProfileControllerTest, KeyActuationWarnsWhenTheKeyboardStoresSomethingElse) {
    gateway.keyFirmwareOverride = ui::KeyActuation{}; // the keyboard ignores the values
    controller.start();

    controller.applyKeyActuation(0, {{2, 2}}, ui::KeyActuation{ui::KeyActuation::MODE_REGULAR, 10, 0, 0}, false);

    ASSERT_TRUE(waitForStatus(StatusSeverity::Warning));
    EXPECT_TRUE(contains(model.snapshot().status.text, "1 of 1 keys report different values"));
}

TEST_F(ProfileControllerTest, KeyActuationIsClampedAndCanBeSaved) {
    controller.start();

    controller.applyKeyActuation(0, {{0, 0}}, ui::KeyActuation{1, 90, 0, 0}, true);

    ASSERT_TRUE(waitForStatus(StatusSeverity::Success));
    EXPECT_EQ(gateway.countOf("applyKeys:0:1:1:39"), 1);
    EXPECT_EQ(gateway.countOf("save:0"), 1);
    EXPECT_TRUE(contains(model.snapshot().status.text, "Saved to the keyboard's flash"));
}

TEST_F(ProfileControllerTest, KeyActuationWithoutReadBackShowsWhatWasSent) {
    gateway.readSupported = false;
    controller.start();
    const ui::KeyActuation value{ui::KeyActuation::MODE_REGULAR, 12, 0, 0};

    controller.applyKeyActuation(2, {{4, 4}}, value, false);

    ASSERT_TRUE(waitForStatus(StatusSeverity::Info));
    ASSERT_TRUE(model.snapshot().keyMatrix[2].has_value());
    EXPECT_EQ(model.snapshot().keyMatrix[2]->at({4, 4}), value);
}

TEST_F(ProfileControllerTest, ReadActuationAlsoReadsTheKeys) {
    gateway.hardwareKeys.at({3, 3}) = ui::KeyActuation{2, 2, 1, 1};
    controller.start();

    controller.readActuation(1);

    ASSERT_TRUE(waitForStatus(StatusSeverity::Info));
    ASSERT_TRUE(model.snapshot().keyMatrix[1].has_value());
    EXPECT_EQ(model.snapshot().keyMatrix[1]->overrideCount(), 1u);
    EXPECT_TRUE(contains(model.snapshot().status.text, "1 keys with their own settings"));
}

// ---------------------------------------------------------------------------
// Profile library
// ---------------------------------------------------------------------------

TEST_F(ProfileControllerTest, LibraryIsPublishedWithTheSlots) {
    gateway.library = {{"Default", "default.json", 0, ActuationSettings{}, 0, {}}, {"Spare", "spare.json", std::nullopt, std::nullopt, 0, {}}};
    controller.start();

    ASSERT_TRUE(waitUntil([&] { return model.snapshot().library.size() == 2; }));
    EXPECT_FALSE(model.snapshot().library[1].assignedSlot.has_value());
}

TEST_F(ProfileControllerTest, AssignClearAndReorderGoThroughTheGateway) {
    controller.start();

    controller.assignProfileToSlot("spare.json", 2, false);
    ASSERT_TRUE(waitForStatus(StatusSeverity::Success));
    controller.clearSlot(1);
    ASSERT_TRUE(waitUntil([&] { return gateway.countOf("clear:1") == 1 && !model.snapshot().busy; }));
    controller.reorderLibrary(1, 0);
    ASSERT_TRUE(waitUntil([&] { return gateway.countOf("reorder:1:0") == 1; }));

    EXPECT_EQ(gateway.countOf("assign:spare.json:2"), 1);
    EXPECT_EQ(gateway.countOf("flash"), 0) << "assigning alone does not write";
}

TEST_F(ProfileControllerTest, AssignAndWriteWritesTheSlotAndVerifiesIt) {
    gateway.hardware = gateway.slots[1].storedSettings;
    controller.start();

    controller.assignProfileToSlot("tekken.json", 1, true);

    ASSERT_TRUE(waitForStatus(StatusSeverity::Success));
    EXPECT_EQ(gateway.countOf("assign:tekken.json:1"), 1) << "assigned without the gateway writing on its own";
    EXPECT_EQ(gateway.countOf("flash:1"), 1);
    EXPECT_TRUE(contains(model.snapshot().status.text, "Verified"));
}

TEST_F(ProfileControllerTest, FailedAssignmentIsAnErrorAndWritesNothing) {
    gateway.assignResult = {false, "Profile file not found: x.json"};
    controller.start();

    controller.assignProfileToSlot("x.json", 0, true);

    ASSERT_TRUE(waitForStatus(StatusSeverity::Error));
    EXPECT_EQ(gateway.countOf("flash"), 0);
}

TEST_F(ProfileControllerTest, FailedReorderIsReportedWithoutBlocking) {
    gateway.reorderResult = {false, "That profile cannot be moved there."};
    controller.start();

    controller.reorderLibrary(4, 9);

    ASSERT_TRUE(waitForStatus(StatusSeverity::Warning));
    EXPECT_FALSE(model.snapshot().busy);
}

// ---------------------------------------------------------------------------
// Watcher events (the controller is an Files::IFileObserver / kbd::IDeviceObserver)
// ---------------------------------------------------------------------------

TEST_F(ProfileControllerTest, FileEventsAreForwardedAndRefreshTheSlots) {
    controller.start();
    ASSERT_TRUE(waitUntil([&] { return model.snapshot().slots[0].hasProfile; }));
    gateway.slots[2] = {true, "Gamepad", "gamepad.json", std::nullopt, std::nullopt};

    controller.onFileCreated("/profiles/gamepad.json");

    ASSERT_TRUE(waitUntil([&] { return model.snapshot().slots[2].hasProfile; }));
    EXPECT_EQ(gateway.countOf("fileCreated:gamepad.json"), 1);
    EXPECT_EQ(model.snapshot().status.text, "Profile file added: gamepad.json");

    gateway.slots[2] = {false, "Slot 3", "", std::nullopt, std::nullopt};
    controller.onFileDeleted("/profiles/gamepad.json");

    ASSERT_TRUE(waitUntil([&] { return !model.snapshot().slots[2].hasProfile; }));
    EXPECT_EQ(gateway.countOf("fileDeleted:gamepad.json"), 1);
}

TEST_F(ProfileControllerTest, DeviceConnectedUpdatesTransportAndReappliesViaTheGateway) {
    gateway.setDevice({false, kbd::TransportMode::Disconnected, ""});
    controller.start();
    ASSERT_TRUE(waitUntil([&] { return model.snapshot().status.serial == 0 && !model.snapshot().device.connected; }));

    gateway.setDevice({true, kbd::TransportMode::Wired, "usb-path"});
    controller.onDeviceConnected(kbd::TransportMode::Wired, "usb-path");

    ASSERT_TRUE(waitForStatus(StatusSeverity::Success));
    const auto state = model.snapshot();
    EXPECT_TRUE(state.device.connected);
    EXPECT_EQ(state.device.transport, kbd::TransportMode::Wired);
    EXPECT_EQ(state.device.path, "usb-path");
    EXPECT_EQ(gateway.countOf("deviceConnected:Wired USB-C:usb-path"), 1);
    EXPECT_TRUE(contains(state.status.text, "Wired USB-C"));
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

// ---------------------------------------------------------------------------
// Automatic gamepad mode (XInput follows the active slot's bindings)
// ---------------------------------------------------------------------------

namespace {
    ui::SlotDetails detailsWithOnePadKey() {
        ui::SlotDetails details;
        details.set({2, 2}, ui::KeyAdvanced{kbd::AdvancedMode::GAMEPAD, kbd::GamepadTarget::LEFT_STICK_UP});
        return details;
    }
}

TEST_F(ProfileControllerTest, ActivatingAGamepadSlotTurnsXInputOnAndAsksForAReplug) {
    gateway.controller.mode = kbd::ControllerMode::TYPING;
    gateway.enumerated = false; // the PC enumerated the HID joystick
    gateway.hardwareDetails = detailsWithOnePadKey();
    controller.setAutoControllerMode(true);
    controller.start();

    controller.activateSlot(2);

    ASSERT_TRUE(waitUntil([&] { return model.snapshot().controller.replugNeeded; }));
    EXPECT_EQ(gateway.controller.mode, kbd::ControllerMode::TYPING | kbd::ControllerMode::XINPUT);
    const auto state = model.snapshot();
    EXPECT_EQ(state.status.severity, StatusSeverity::Success);
    EXPECT_TRUE(contains(state.status.text, "XInput mode was turned on"));
    EXPECT_TRUE(state.details[2].has_value());
}

TEST_F(ProfileControllerTest, ActivatingAKeyboardSlotTurnsXInputOffAndKeepsTyping) {
    gateway.controller.mode = kbd::ControllerMode::XINPUT | kbd::ControllerMode::TYPING;
    gateway.enumerated = true;
    controller.setAutoControllerMode(true);
    controller.start();

    controller.activateSlot(1);

    ASSERT_TRUE(waitUntil([&] { return model.snapshot().controller.replugNeeded; }));
    EXPECT_EQ(gateway.controller.mode, kbd::ControllerMode::TYPING);
    EXPECT_TRUE(contains(model.snapshot().status.text, "turned off"));
}

TEST_F(ProfileControllerTest, MatchingModeIsLeftAloneAndNeedsNoReplug) {
    gateway.controller.mode = kbd::ControllerMode::XINPUT;
    gateway.hardwareDetails = detailsWithOnePadKey();
    controller.setAutoControllerMode(true);
    controller.start();

    controller.activateSlot(1);

    ASSERT_TRUE(waitForStatus(StatusSeverity::Success));
    ASSERT_TRUE(waitUntil([&] { return model.snapshot().details[1].has_value(); }));
    EXPECT_EQ(gateway.countOf("controllerMode"), 0);
    EXPECT_FALSE(model.snapshot().controller.replugNeeded);
}

TEST_F(ProfileControllerTest, DisabledAutomaticModeNeverTouchesTheControllerMode) {
    gateway.controller.mode = kbd::ControllerMode::TYPING;
    gateway.hardwareDetails = detailsWithOnePadKey();
    controller.start();

    controller.activateSlot(2);

    ASSERT_TRUE(waitForStatus(StatusSeverity::Success));
    EXPECT_EQ(gateway.controller.mode, kbd::ControllerMode::TYPING);
    EXPECT_FALSE(model.snapshot().controller.replugNeeded);
}

// ---------------------------------------------------------------------------
// Replug banner: stored mode versus what the PC enumerated
// ---------------------------------------------------------------------------

TEST(ControllerSettingsTest, ReplugIsNeededOnlyWhenStoredAndEnumeratedModesDisagree) {
    const ui::ControllerSettings xinput{std::uint8_t{kbd::ControllerMode::XINPUT}, std::nullopt, std::nullopt, false};
    EXPECT_FALSE(xinput.withEnumeration(true).replugNeeded);
    EXPECT_TRUE(xinput.withEnumeration(false).replugNeeded);
    EXPECT_FALSE(xinput.withEnumeration(std::nullopt).replugNeeded) << "unknown enumeration never raises the banner";
    const ui::ControllerSettings joystick{std::uint8_t{kbd::ControllerMode::TYPING}, std::nullopt, std::nullopt, false};
    EXPECT_TRUE(joystick.withEnumeration(true).replugNeeded);
    EXPECT_FALSE(joystick.withEnumeration(false).replugNeeded);
    EXPECT_FALSE(ui::ControllerSettings{}.withEnumeration(true).replugNeeded) << "unknown stored mode never raises it";
}

TEST_F(ProfileControllerTest, ConnectingWithMatchingModesShowsNoBannerAndChangesNothing) {
    gateway.controller.mode = kbd::ControllerMode::XINPUT | kbd::ControllerMode::TYPING;
    gateway.enumerated = true;
    gateway.activeSlot = 0; // a keyboard-only slot: the automatic mode must still leave the mode alone
    controller.setAutoControllerMode(true);
    controller.start();

    controller.onDeviceConnected(kbd::TransportMode::Wired, "fake-hid-path");

    ASSERT_TRUE(waitUntil([&] { return model.snapshot().controller.enumeratedXInput.has_value(); }));
    std::this_thread::sleep_for(150ms); // several slot polls
    EXPECT_FALSE(model.snapshot().controller.replugNeeded);
    EXPECT_EQ(gateway.countOf("controllerMode"), 0) << "plugging in never switches the mode";
}

TEST_F(ProfileControllerTest, ConnectingAfterAnEarlierModeChangeShowsTheBanner) {
    gateway.controller.mode = kbd::ControllerMode::XINPUT; // stored for the next start
    gateway.enumerated = false;                          // but the PC still runs the HID joystick
    controller.start();

    controller.onDeviceConnected(kbd::TransportMode::Wired, "fake-hid-path");

    ASSERT_TRUE(waitUntil([&] { return model.snapshot().controller.replugNeeded; }));
}

TEST_F(ProfileControllerTest, BannerClearsWhenThePcEnumeratesTheNewMode) {
    gateway.controller.mode = kbd::ControllerMode::XINPUT;
    gateway.enumerated = false;
    controller.start();
    controller.onDeviceConnected(kbd::TransportMode::Wired, "fake-hid-path");
    ASSERT_TRUE(waitUntil([&] { return model.snapshot().controller.replugNeeded; }));

    controller.setPollingEnabled(true); // the window is open: the slot poll also watches the device list
    gateway.enumerated = true;           // e.g. Windows restarted the USB device

    EXPECT_TRUE(waitUntil([&] { return !model.snapshot().controller.replugNeeded; }));
}
