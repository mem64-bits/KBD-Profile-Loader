#include <gtest/gtest.h>

#include "kbd/FirmwareModel.h"

#include <vector>

using namespace kbd;
using namespace kbd::model;

namespace {

    XInputReport stepOnce(XInputModel& model, std::vector<GamepadKey> keys, const ResponseCurve& curve = DEFAULT_CURVE) {
        return model.step(keys, curve);
    }

} // namespace

TEST(XInputModel, FullStickDirectionsReachTheEdges) {
    XInputModel model;
    EXPECT_EQ(stepOnce(model, {{GamepadTarget::LEFT_STICK_UP, 240, true}}).leftY, 32767);
    model.reset();
    EXPECT_EQ(stepOnce(model, {{GamepadTarget::LEFT_STICK_LEFT, 240, true}}).leftX, -32768);
    model.reset();
    const XInputReport down = stepOnce(model, {{GamepadTarget::RIGHT_STICK_DOWN, 240, true}});
    EXPECT_EQ(down.rightY, -32768);
    EXPECT_EQ(down.leftX, 0);
}

TEST(XInputModel, HalfTravelFollowsTheCurve) {
    XInputModel model;
    // 2.0 mm = 120 units: between (1.0 mm, 31) and (3.0 mm, 95), slope 3.2 -> 31 + 3.2 * 10 = 63 -> 63 * 32767 / 127.
    EXPECT_EQ(stepOnce(model, {{GamepadTarget::LEFT_STICK_RIGHT, 120, true}}).leftX, 63 * 32767 / 127);
    // Under one 0.1 mm step the key counts as released.
    EXPECT_EQ(stepOnce(model, {{GamepadTarget::LEFT_STICK_RIGHT, 5, false}}).leftX, 0);
}

TEST(XInputModel, DiagonalsAreScaledOntoTheCircle) {
    XInputModel model;
    const XInputReport r = stepOnce(model, {{GamepadTarget::LEFT_STICK_UP, 240, true}, {GamepadTarget::LEFT_STICK_RIGHT, 240, true}});
    // (127, 127) in the >> 8 domain: |v| = isqrt(32258) = 179, ratio 140 / 179.
    const auto expected = static_cast<std::int16_t>(static_cast<float>(32767) * (140.0f / 179.0f));
    EXPECT_EQ(r.leftX, expected);
    EXPECT_EQ(r.leftY, expected);
}

TEST(XInputModel, OpposingKeysKeepTheirDirectionUntilOneIsClearlyDeeper) {
    XInputModel model;
    stepOnce(model, {{GamepadTarget::LEFT_STICK_LEFT, 240, true}, {GamepadTarget::LEFT_STICK_RIGHT, 0, false}});
    // Both full: neither is 2 x TRAVEL_SCALE deeper, so the stick stays left.
    EXPECT_EQ(stepOnce(model, {{GamepadTarget::LEFT_STICK_LEFT, 240, true}, {GamepadTarget::LEFT_STICK_RIGHT, 240, true}}).leftX, -32768);
    // Left key comes up a bit: right is now clearly deeper.
    EXPECT_GT(stepOnce(model, {{GamepadTarget::LEFT_STICK_LEFT, 200, true}, {GamepadTarget::LEFT_STICK_RIGHT, 240, true}}).leftX, 0);
}

TEST(XInputModel, TriggersAreLinearAndButtonsUseTheActuation) {
    XInputModel model;
    ResponseCurve flat{{{0, 0}, {10, 0}, {30, 0}, {40, 0}}}; // the curve must not touch triggers
    const XInputReport r = stepOnce(model, {{GamepadTarget::LEFT_TRIGGER, 240, true},
                                            {GamepadTarget::RIGHT_TRIGGER, 120, true},
                                            {GamepadTarget::button(0), 30, true},
                                            {GamepadTarget::button(13), 240, false}},
                                    flat);
    EXPECT_EQ(r.leftTrigger, 255);
    EXPECT_EQ(r.rightTrigger, 63 * 255 / 127);
    EXPECT_EQ(r.buttons, XButton::A); // D-pad right is deep but not actuated
    EXPECT_EQ(xinputButtonBit(GamepadTarget::button(6)), XButton::BACK);
    EXPECT_EQ(xinputButtonBit(GamepadTarget::button(14)), XButton::GUIDE);
    EXPECT_EQ(xinputButtonBit(GamepadTarget::LEFT_STICK_UP), 0);
}

TEST(SocdPairModel, NeutralCancelsBoth) {
    SocdPairModel pair;
    EXPECT_EQ(pair.step(SocdType::NEUTRAL, {true, 200}, {false, 0}), (SocdOutput{true, false}));
    EXPECT_EQ(pair.step(SocdType::NEUTRAL, {true, 200}, {true, 200}), (SocdOutput{false, false}));
    EXPECT_EQ(pair.step(SocdType::NEUTRAL, {false, 0}, {true, 200}), (SocdOutput{false, true}));
}

TEST(SocdPairModel, LastKeystrokeWinsAndHandsBack) {
    SocdPairModel pair;
    EXPECT_EQ(pair.step(SocdType::LAST_KEYSTROKE, {true, 200}, {false, 0}), (SocdOutput{true, false}));
    EXPECT_EQ(pair.step(SocdType::LAST_KEYSTROKE, {true, 200}, {true, 200}), (SocdOutput{false, true}));
    EXPECT_EQ(pair.step(SocdType::LAST_KEYSTROKE, {true, 200}, {true, 200}), (SocdOutput{false, true})); // held: keeps
    EXPECT_EQ(pair.step(SocdType::LAST_KEYSTROKE, {true, 200}, {false, 0}), (SocdOutput{true, false}));  // D up: A back
}

TEST(SocdPairModel, PriorityKeys) {
    SocdPairModel pair;
    EXPECT_EQ(pair.step(SocdType::KEY_1, {true, 50}, {true, 200}), (SocdOutput{true, false}));
    pair.reset();
    EXPECT_EQ(pair.step(SocdType::KEY_2, {true, 200}, {true, 50}), (SocdOutput{false, true}));
}

TEST(SocdPairModel, DeeperPressWins) {
    SocdPairModel pair;
    EXPECT_EQ(pair.step(SocdType::DEEPER_TRAVEL, {true, 100}, {false, 0}), (SocdOutput{true, false}));
    EXPECT_EQ(pair.step(SocdType::DEEPER_TRAVEL, {true, 100}, {true, 50}), (SocdOutput{true, false}));
    EXPECT_EQ(pair.step(SocdType::DEEPER_TRAVEL, {true, 100}, {true, 150}), (SocdOutput{false, true}));
    // Both bottomed out: mode 1 sends both, mode 2 keeps the last key.
    EXPECT_EQ(pair.step(SocdType::DEEPER_TRAVEL, {true, 240}, {true, 240}), (SocdOutput{true, true}));
    SocdPairModel single;
    single.step(SocdType::DEEPER_TRAVEL_SINGLE, {true, 100}, {false, 0});
    EXPECT_EQ(single.step(SocdType::DEEPER_TRAVEL_SINGLE, {true, 240}, {true, 240}), (SocdOutput{true, false}));
}

TEST(SocdPairModel, NoneSendsBoth) {
    SocdPairModel pair;
    EXPECT_EQ(pair.step(SocdType::NONE, {true, 100}, {true, 100}), (SocdOutput{true, true}));
}

TEST(KeyTriggerModel, RegularKeyUsesActuationAndHysteresis) {
    KeyTriggerModel key;
    const TriggerSettings settings{20, false, 3, 3};
    EXPECT_FALSE(key.step(travelFromTenths(19), settings));
    EXPECT_TRUE(key.step(travelFromTenths(20), settings));
    EXPECT_TRUE(key.step(travelFromTenths(18), settings));  // 0.3 mm hysteresis: still down at 1.8 mm
    EXPECT_FALSE(key.step(travelFromTenths(16), settings)); // under 1.7 mm: up
}

TEST(KeyTriggerModel, RapidTriggerReleasesOnTheWayUpAndPressesOnTheWayDown) {
    KeyTriggerModel key;
    const TriggerSettings settings{5, true, 2, 3};
    EXPECT_TRUE(key.step(travelFromTenths(5), settings));
    EXPECT_TRUE(key.step(travelFromTenths(25), settings));  // deeper: the release point follows (2.2 mm)
    EXPECT_TRUE(key.step(travelFromTenths(23), settings));
    EXPECT_FALSE(key.step(travelFromTenths(22), settings)); // up 0.3 mm: released, far above the actuation point
    EXPECT_FALSE(key.step(travelFromTenths(20), settings)); // further up: the press point follows (2.2 mm)
    EXPECT_FALSE(key.step(travelFromTenths(21), settings));
    EXPECT_TRUE(key.step(travelFromTenths(22), settings));  // down 0.2 mm: pressed again
    EXPECT_FALSE(key.step(travelFromTenths(1), settings));  // all the way up: a normal release
}
