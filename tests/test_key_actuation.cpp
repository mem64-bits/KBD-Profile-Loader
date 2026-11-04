#include <gtest/gtest.h>

#include "kbd/ProfileMemory.h"
#include "ui/ActuationModel.h"

#include <vector>

using ui::ActuationSettings;
using ui::KeyActuation;

TEST(KeyActuationTest, DefaultKeyFollowsTheSlot) {
    const ActuationSettings global{true, 5, 2, 3};
    EXPECT_TRUE(KeyActuation{}.followsGlobal());
    EXPECT_EQ(ui::actuation::effective(global, KeyActuation{}), global);
}

TEST(KeyActuationTest, OwnValuesReplaceOnlyTheFieldsThatAreSet) {
    const ActuationSettings global{true, 5, 2, 3};
    EXPECT_EQ(ui::actuation::effective(global, KeyActuation{0, 12, 0, 0}), (ActuationSettings{true, 12, 2, 3}));
    EXPECT_EQ(ui::actuation::effective(global, KeyActuation{KeyActuation::MODE_REGULAR, 0, 0, 0}), (ActuationSettings{false, 5, 2, 3}));
    EXPECT_EQ(ui::actuation::effective(global, KeyActuation{KeyActuation::MODE_RAPID, 2, 1, 1}), (ActuationSettings{true, 2, 1, 1}));
}

TEST(KeyActuationTest, AnOverridePinsEveryValue) {
    const KeyActuation key = ui::actuation::overrideFrom(ActuationSettings{false, 0, 50, 4});
    EXPECT_EQ(key, (KeyActuation{KeyActuation::MODE_REGULAR, 1, 39, 4})) << "clamped into the firmware range";
    EXPECT_FALSE(key.followsGlobal());
}

TEST(KeyActuationTest, SanitizingKeepsZeroAsFollowGlobal) {
    EXPECT_EQ(ui::actuation::sanitized(KeyActuation{7, 0, 60, 0}), (KeyActuation{2, 0, 39, 0}));
}

TEST(KeyActuationTest, KeyMatrixIsDecodedFromAProfileDump) {
    std::vector<std::uint8_t> memory(kbd::ProfileMemory::PROFILE_SIZE, 0);
    ASSERT_TRUE(kbd::ProfileMemory::encodeKeyTravel(memory, 3, 1, {2, 3, 1, 1, 0, 0}));

    const auto matrix = ui::actuation::keyMatrixFromProfile(memory);
    ASSERT_TRUE(matrix);
    EXPECT_EQ(matrix->at({3, 1}), (KeyActuation{2, 3, 1, 1}));
    EXPECT_TRUE(matrix->at({3, 2}).followsGlobal());
    EXPECT_EQ(matrix->overrideCount(), 1u);

    EXPECT_FALSE(ui::actuation::keyMatrixFromProfile(std::vector<std::uint8_t>(100, 0)).has_value());
}

TEST(KeyActuationTest, DescriptionSaysWhetherAKeyFollowsTheSlot) {
    const ActuationSettings global{false, 20, 4, 4};
    EXPECT_NE(ui::actuation::describe(KeyActuation{}, global).find("follows the slot"), std::string::npos);
    EXPECT_EQ(ui::actuation::describe(KeyActuation{2, 3, 1, 1}, global), "0.3 mm, Rapid Trigger 0.1 mm / 0.1 mm");
}
