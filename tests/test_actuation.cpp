#include <gtest/gtest.h>

#include "ui/ActuationModel.h"

#include <cmath>

using ui::ActuationSettings;
namespace act = ui::actuation;

TEST(ActuationModelTest, DefaultsMatchTheFirmwareDefaults) {
    const ActuationSettings defaults;
    EXPECT_FALSE(defaults.rapidTrigger);
    EXPECT_EQ(defaults.actuationTenths, 20); // 2.0 mm
    EXPECT_EQ(defaults.pressTenths, 4);      // 0.4 mm
    EXPECT_EQ(defaults.releaseTenths, 4);
}

TEST(ActuationModelTest, MillimetreConversionIsExactInTenths) {
    EXPECT_FLOAT_EQ(act::toMillimetres(3), 0.3f);
    EXPECT_FLOAT_EQ(act::toMillimetres(39), 3.9f);

    EXPECT_EQ(act::fromMillimetres(0.3f), 3);
    EXPECT_EQ(act::fromMillimetres(1.4f), 14);
    EXPECT_EQ(act::fromMillimetres(0.34f), 3);  // rounds to the nearest tenth
    EXPECT_EQ(act::fromMillimetres(0.36f), 4);
}

TEST(ActuationModelTest, ConversionClampsToWhatTheFirmwareAccepts) {
    EXPECT_EQ(act::fromMillimetres(-5.0f), act::MIN_TENTHS);
    EXPECT_EQ(act::fromMillimetres(0.0f), act::MIN_TENTHS); // 0 would mean "use the default" on the keyboard
    EXPECT_EQ(act::fromMillimetres(4.0f), act::MAX_TENTHS); // 4.0 mm is rejected (limit is 3.9)
    EXPECT_EQ(act::fromMillimetres(100.0f), act::MAX_TENTHS);
    EXPECT_EQ(act::fromMillimetres(std::nanf("")), act::DEFAULT_ACTUATION_TENTHS);

    EXPECT_EQ(act::clampTenths(-1), 1);
    EXPECT_EQ(act::clampTenths(0), 1);
    EXPECT_EQ(act::clampTenths(40), 39);
    EXPECT_EQ(act::clampTenths(255), 39);
}

TEST(ActuationModelTest, SanitizedClampsEveryDistanceAndKeepsTheMode) {
    const ActuationSettings wild{true, 0, 200, 40};

    const ActuationSettings clean = act::sanitized(wild);

    EXPECT_TRUE(clean.rapidTrigger);
    EXPECT_EQ(clean.actuationTenths, 1);
    EXPECT_EQ(clean.pressTenths, 39);
    EXPECT_EQ(clean.releaseTenths, 39);
    EXPECT_EQ(act::sanitized(clean), clean) << "sanitizing must be idempotent";
}

TEST(ActuationModelTest, GlobalTravelDecodingFollowsTheFirmwareLoadRules) {
    // Rapid Trigger profile from a Launcher dump.
    const auto rapid = act::fromGlobalTravel({kbd::GlobalTravelConfig::MODE_RAPID, 3, 1, 2, 0, 0});
    EXPECT_EQ(rapid, (ActuationSettings{true, 3, 1, 2}));

    // Regular mode is *not* Rapid Trigger.
    const auto regular = act::fromGlobalTravel({kbd::GlobalTravelConfig::MODE_REGULAR, 14, 4, 4, 0, 0});
    EXPECT_FALSE(regular.rapidTrigger);
    EXPECT_EQ(regular.actuationTenths, 14);

    // 0 / out-of-range values fall back to defaults; a release sensitivity of 0 means "same as press".
    const auto blank = act::fromGlobalTravel({kbd::GlobalTravelConfig::MODE_REGULAR, 0, 0, 0, 0, 0});
    EXPECT_EQ(blank, ActuationSettings{});
    const auto outOfRange = act::fromGlobalTravel({kbd::GlobalTravelConfig::MODE_RAPID, 63, 63, 0, 0, 0});
    EXPECT_EQ(outOfRange.actuationTenths, ui::actuation::DEFAULT_ACTUATION_TENTHS);
    EXPECT_EQ(outOfRange.pressTenths, ui::actuation::DEFAULT_SENSITIVITY_TENTHS);
    EXPECT_EQ(outOfRange.releaseTenths, outOfRange.pressTenths);

    // The firmware's own limit for loaded profiles is 40; the setter only accepts 39, so we clamp.
    EXPECT_EQ(act::fromGlobalTravel({kbd::GlobalTravelConfig::MODE_REGULAR, 40, 4, 4, 0, 0}).actuationTenths, 39);
}

TEST(ActuationModelTest, FormattingIsHumanReadable) {
    EXPECT_EQ(act::formatMillimetres(3), "0.3 mm");
    EXPECT_EQ(act::formatMillimetres(20), "2.0 mm");
    EXPECT_EQ(act::describe({false, 20, 4, 4}), "2.0 mm, Rapid Trigger off");
    EXPECT_EQ(act::describe({true, 3, 1, 2}), "0.3 mm, Rapid Trigger 0.1 mm / 0.2 mm");
}

TEST(ActuationModelTest, PresetsAreValidForTheFirmware) {
    const auto presets = act::presets();
    ASSERT_FALSE(presets.empty());

    for (const auto& preset : presets) {
        EXPECT_FALSE(preset.name.empty());
        EXPECT_EQ(act::sanitized(preset.settings), preset.settings) << preset.name << " has out-of-range values";
    }
    // The first preset restores the firmware defaults.
    EXPECT_EQ(presets.front().settings, ActuationSettings{});
}
