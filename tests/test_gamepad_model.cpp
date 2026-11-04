#include <gtest/gtest.h>

#include "ui/DemoGamepadSource.h"
#include "ui/GamepadModel.h"

#include <cmath>
#include <numbers>

using namespace ui::gamepad;

namespace {

    PadState stick(const float x, const float y) {
        PadState state;
        state.set(Axis::LeftX, x);
        state.set(Axis::LeftY, y);
        return state;
    }

    constexpr std::uint64_t MS = 1'000'000ULL;

} // namespace

TEST(NumpadDirectionTest, StickDirectionsFollowNumpadNotation) {
    EXPECT_EQ(numpadDirection(stick(0, 0)), 5);
    EXPECT_EQ(numpadDirection(stick(0, 1)), 8);
    EXPECT_EQ(numpadDirection(stick(0, -1)), 2);
    EXPECT_EQ(numpadDirection(stick(1, 0)), 6);
    EXPECT_EQ(numpadDirection(stick(-1, 0)), 4);
    EXPECT_EQ(numpadDirection(stick(1, -1)), 3) << "a keyboard stick reaches the corner of the square";
    EXPECT_EQ(numpadDirection(stick(-0.7f, 0.7f)), 7);
    EXPECT_EQ(numpadDirection(stick(0.3f, 0.3f)), 5) << "inside the threshold the stick is neutral";
}

TEST(NumpadDirectionTest, DpadWinsOverTheStick) {
    PadState state = stick(1, 0);
    state.set(Button::DpadDown, true);
    state.set(Button::DpadLeft, true);
    EXPECT_EQ(numpadDirection(state), 1);
}

TEST(AttackLabelTest, NotationsNameTheButtonsTheirWay) {
    EXPECT_STREQ(attackLabel(Attack::West, Notation::Tekken, LabelStyle::Xbox), "1");
    EXPECT_STREQ(attackLabel(Attack::East, Notation::Tekken, LabelStyle::Xbox), "4");
    EXPECT_STREQ(attackLabel(Attack::RightTrigger, Notation::StreetFighter, LabelStyle::Xbox), "HK");
    EXPECT_STREQ(attackLabel(Attack::RightShoulder, Notation::StreetFighter, LabelStyle::Xbox), "HP");
    EXPECT_STREQ(attackLabel(Attack::South, Notation::PadLabels, LabelStyle::PlayStation), "Cross");
    // Tekken has no name for the bumpers: the pad's own name is used.
    EXPECT_STREQ(attackLabel(Attack::LeftShoulder, Notation::Tekken, LabelStyle::Xbox), "LB");
    EXPECT_STREQ(buttonName(Button::Start, LabelStyle::PlayStation), "Options");
}

TEST(AttackLabelTest, TriggersCountAsButtonsPastHalfway) {
    PadState state;
    state.set(Axis::RightTrigger, 0.49f);
    EXPECT_FALSE(attacksOf(state)[static_cast<std::size_t>(Attack::RightTrigger)]);
    state.set(Axis::RightTrigger, 0.5f);
    EXPECT_TRUE(attacksOf(state)[static_cast<std::size_t>(Attack::RightTrigger)]);
}

TEST(InputHistoryTest, AQuarterCircleForwardPunchReadsBackNewestFirst) {
    InputHistory history(16, 60.0);
    PadState punch = stick(1, 0);
    punch.set(Button::West, true);

    history.push({0, stick(0, 0)});
    history.push({100 * MS, stick(0, -1)});  // 2
    history.push({150 * MS, stick(1, -1)});  // 3
    history.push({200 * MS, stick(1, 0)});   // 6
    history.push({250 * MS, punch});         // 6 + X
    history.push({300 * MS, stick(0, 0)});

    const auto& entries = history.entries();
    ASSERT_EQ(entries.size(), 6u);
    EXPECT_EQ(entries[0].direction, 5);
    EXPECT_EQ(entries[1].direction, 6);
    EXPECT_TRUE(entries[1].pressed[static_cast<std::size_t>(Attack::West)]);
    EXPECT_EQ(entries[2].direction, 6);
    EXPECT_FALSE(entries[2].anyHeld());
    EXPECT_EQ(entries[3].direction, 3);
    EXPECT_EQ(entries[4].direction, 2);
    EXPECT_EQ(history.frames(entries[3], 0), 3u) << "50 ms at 60 fps";
    EXPECT_EQ(history.frames(entries[0], 400 * MS), 6u) << "the current entry runs until now";
}

TEST(InputHistoryTest, StickJitterInsideTheSameInputAddsNoLine) {
    InputHistory history;
    history.push({0, stick(1, 0)});
    history.push({5 * MS, stick(0.98f, 0.02f)});
    history.push({9 * MS, stick(0.95f, -0.04f)});
    EXPECT_EQ(history.entries().size(), 1u);
}

TEST(InputHistoryTest, CapacityDropsTheOldest) {
    InputHistory history(3);
    for (int i = 0; i < 10; ++i) history.push({static_cast<std::uint64_t>(i) * MS, stick(i % 2 == 0 ? 1.0f : -1.0f, 0)});
    EXPECT_EQ(history.entries().size(), 3u);
    history.clear();
    EXPECT_TRUE(history.entries().empty());
}

TEST(CircularityTestTest, ARoundStickScoresNearZeroAndAKeyboardStickReachesTheCorners) {
    CircularityTest round;
    for (int i = 0; i < 720; ++i) {
        const double a = i * std::numbers::pi / 360.0;
        round.add(static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a)));
    }
    ASSERT_TRUE(round.averageErrorPercent());
    EXPECT_LT(*round.averageErrorPercent(), 0.5f);
    EXPECT_FLOAT_EQ(round.coverage(), 1.0f);

    CircularityTest square;
    for (const auto& [x, y] : {std::pair{1.0f, 1.0f}, {-1.0f, 1.0f}, {1.0f, -1.0f}, {-1.0f, -1.0f}}) square.add(x, y);
    EXPECT_NEAR(square.maxRadius(), std::sqrt(2.0f), 1e-4f);
    EXPECT_NEAR(*square.averageErrorPercent(), 41.42f, 0.05f);

    CircularityTest centre;
    centre.add(0.1f, 0.1f);
    EXPECT_FALSE(centre.averageErrorPercent()) << "the centre carries no angle";
}

TEST(StickTrailTest, OldPointsExpire) {
    StickTrail trail(100 * MS);
    trail.push(0, 0, 0);
    trail.push(1, 0, 60 * MS);
    trail.push(1, 1, 120 * MS);
    trail.expire(150 * MS);
    ASSERT_EQ(trail.points().size(), 2u);
    EXPECT_EQ(trail.points().front().timestampNs, 60 * MS);
}

TEST(DemoGamepadSourceTest, TheScriptStartsNeutralThenPlaysAQuarterCircle) {
    constexpr std::uint64_t FRAME = 1'000'000'000ULL / 60;
    const PadState start = ui::DemoGamepadSource::stateAt(0);
    EXPECT_EQ(numpadDirection(start), 5);
    EXPECT_EQ(numpadDirection(ui::DemoGamepadSource::stateAt(41 * FRAME)), 2);
    EXPECT_EQ(numpadDirection(ui::DemoGamepadSource::stateAt(45 * FRAME)), 3);
    const PadState punch = ui::DemoGamepadSource::stateAt(48 * FRAME);
    EXPECT_EQ(numpadDirection(punch), 6);
    EXPECT_TRUE(punch.pressed(Button::West));

    ui::DemoGamepadSource source;
    ASSERT_EQ(source.pads().size(), 1u);
    EXPECT_TRUE(source.pads().front().isKeychron());
}
