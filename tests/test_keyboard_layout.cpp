#include <gtest/gtest.h>

#include "ui/ActuationModel.h"
#include "ui/KeyboardLayout.h"

#include <set>
#include <string>

// The layouts are generated from Keychron's QMK definitions; these checks catch a broken table.

namespace {

    using ui::KeyboardLayout;
    using ui::KeyGeometry;
    using ui::LayoutKind;

    bool overlap(float a0, float a1, float b0, float b1) {
        constexpr float EPS = 0.001f;
        return a0 < b1 - EPS && b0 < a1 - EPS;
    }

    // Rectangles of a key (one, or two for ISO Enter).
    std::vector<std::array<float, 4>> rects(const KeyGeometry& k) {
        std::vector<std::array<float, 4>> out{{k.x, k.y, k.x + k.w, k.y + k.h}};
        if (k.isStepped()) out.push_back({k.x + k.x2, k.y + k.y2, k.x + k.x2 + k.w2, k.y + k.y2 + k.h2});
        return out;
    }

    class LayoutTest : public ::testing::TestWithParam<LayoutKind> {};

} // namespace

TEST(KeyboardLayoutTest, IsoHas85KeysAndAnsi84) {
    EXPECT_EQ(ui::keyboardLayout(LayoutKind::Iso).keys.size(), 85u);
    EXPECT_EQ(ui::keyboardLayout(LayoutKind::Ansi).keys.size(), 84u);
}

TEST(KeyboardLayoutTest, IsoEnterIsTheSteppedKeyAtRow2Column13) {
    const KeyboardLayout& iso = ui::keyboardLayout(LayoutKind::Iso);
    const KeyGeometry* enter = iso.find({2, 13});
    ASSERT_NE(enter, nullptr);
    EXPECT_TRUE(enter->isStepped());
    EXPECT_FLOAT_EQ(enter->h, 2.0f);
    EXPECT_STREQ(enter->legend, "Enter");

    const KeyGeometry* hash = iso.find({3, 12});
    ASSERT_NE(hash, nullptr);
    EXPECT_STREQ(hash->legend, "#");
    const KeyGeometry* home = iso.find({3, 13});
    ASSERT_NE(home, nullptr);
    EXPECT_FLOAT_EQ(home->x, 15.0f) << "Home sits in the right column, not next to #";

    const KeyGeometry* ansiEnter = ui::keyboardLayout(LayoutKind::Ansi).find({3, 12});
    ASSERT_NE(ansiEnter, nullptr);
    EXPECT_STREQ(ansiEnter->legend, "Enter");
    EXPECT_FALSE(ansiEnter->isStepped());
}

TEST_P(LayoutTest, IsSixteenBySixUnits) {
    const KeyboardLayout& layout = ui::keyboardLayout(GetParam());
    EXPECT_FLOAT_EQ(layout.width, 16.0f);
    EXPECT_FLOAT_EQ(layout.height, 6.0f);
}

TEST_P(LayoutTest, MatrixPositionsAreUniqueAndInsideTheFirmwareMatrix) {
    std::set<ui::KeyPosition> seen;
    for (const KeyGeometry& key : ui::keyboardLayout(GetParam()).keys) {
        EXPECT_LT(key.row, ui::MATRIX_ROWS);
        EXPECT_LT(key.col, ui::MATRIX_COLS);
        EXPECT_TRUE(seen.insert(key.position()).second) << "duplicate " << int(key.row) << "," << int(key.col);
    }
}

TEST_P(LayoutTest, KeysDoNotOverlap) {
    const auto& keys = ui::keyboardLayout(GetParam()).keys;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        for (std::size_t j = i + 1; j < keys.size(); ++j) {
            for (const auto& a : rects(keys[i])) {
                for (const auto& b : rects(keys[j])) {
                    EXPECT_FALSE(overlap(a[0], a[2], b[0], b[2]) && overlap(a[1], a[3], b[1], b[3]))
                        << keys[i].legend << " overlaps " << keys[j].legend;
                }
            }
        }
    }
}

TEST_P(LayoutTest, EveryRowIsFilledEdgeToEdge) {
    // Sum of key widths per row (the tall ISO Enter counts in both of its rows).
    for (int row = 0; row < 6; ++row) {
        float covered = 0.0f;
        for (const KeyGeometry& key : ui::keyboardLayout(GetParam()).keys) {
            for (const auto& r : rects(key)) {
                if (r[1] <= row + 0.001f && r[3] >= row + 1 - 0.001f) covered += r[2] - r[0];
            }
        }
        if (GetParam() == LayoutKind::Iso && row == 2) covered -= 1.25f; // Enter's body and top overlap in row 2
        EXPECT_FLOAT_EQ(covered, 16.0f) << "row " << row;
    }
}

TEST_P(LayoutTest, EveryKeyHasALegendOrAGlyphExceptTheSpaceBar) {
    for (const KeyGeometry& key : ui::keyboardLayout(GetParam()).keys) {
        const bool space = key.row == 5 && key.col == 6;
        EXPECT_EQ(std::string(key.legend).empty() && key.glyph == ui::KeyGlyph::None, space) << int(key.row) << "," << int(key.col);
    }
}

INSTANTIATE_TEST_SUITE_P(K2He, LayoutTest, ::testing::Values(LayoutKind::Iso, LayoutKind::Ansi),
                         [](const auto& info) { return info.param == LayoutKind::Iso ? std::string("Iso") : std::string("Ansi"); });
