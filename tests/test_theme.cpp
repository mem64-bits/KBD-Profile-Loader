#include <gtest/gtest.h>

#include "support/TempDir.h"
#include "ui/Color.h"
#include "ui/IThemeObserver.h"
#include "ui/ObserverList.h"
#include "ui/Theme.h"
#include "ui/ThemeManager.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>

// ---------------------------------------------------------------------------
// ObserverList
// ---------------------------------------------------------------------------

namespace {

    struct Listener {
        int calls = 0;
    };

}

TEST(ObserverListTest, IgnoresNullAndDuplicateRegistrations) {
    ui::ObserverList<Listener> list;
    Listener a;

    EXPECT_FALSE(list.add(nullptr));
    EXPECT_TRUE(list.add(&a));
    EXPECT_FALSE(list.add(&a));
    EXPECT_EQ(list.size(), 1u);

    EXPECT_FALSE(list.remove(nullptr));
    EXPECT_TRUE(list.remove(&a));
    EXPECT_FALSE(list.remove(&a));
    EXPECT_EQ(list.size(), 0u);
}

TEST(ObserverListTest, NotifyReachesEveryObserverOnce) {
    ui::ObserverList<Listener> list;
    Listener a, b;
    list.add(&a);
    list.add(&b);

    list.notify([](Listener& l) { ++l.calls; });

    EXPECT_EQ(a.calls, 1);
    EXPECT_EQ(b.calls, 1);
}

TEST(ObserverListTest, ObserverRemovedDuringNotifySkipsLaterCallbacks) {
    ui::ObserverList<Listener> list;
    Listener first, second;
    list.add(&first);
    list.add(&second);

    // `first` unregisters `second` while the round is running: second must not be called.
    list.notify([&](Listener& l) {
        ++l.calls;
        if (&l == &first) list.remove(&second);
    });

    EXPECT_EQ(first.calls, 1);
    EXPECT_EQ(second.calls, 0);
}

TEST(ObserverListTest, ObserverMayUnregisterItselfFromItsCallback) {
    ui::ObserverList<Listener> list;
    Listener a;
    list.add(&a);

    EXPECT_NO_THROW(list.notify([&](Listener& l) {
        ++l.calls;
        list.remove(&l);
    }));
    EXPECT_EQ(list.size(), 0u);
}

// ---------------------------------------------------------------------------
// Color
// ---------------------------------------------------------------------------

TEST(ColorTest, ParsesAllSupportedHexForms) {
    EXPECT_EQ(ui::Color::fromHex("#7C3AED"), ui::Color(0x7C, 0x3A, 0xED));
    EXPECT_EQ(ui::Color::fromHex("7c3aed"), ui::Color(0x7C, 0x3A, 0xED));
    EXPECT_EQ(ui::Color::fromHex("#F0A"), ui::Color(0xFF, 0x00, 0xAA));
    EXPECT_EQ(ui::Color::fromHex("#F0A8"), ui::Color(0xFF, 0x00, 0xAA, 0x88));
    EXPECT_EQ(ui::Color::fromHex("#12345678"), ui::Color(0x12, 0x34, 0x56, 0x78));
}

TEST(ColorTest, RejectsMalformedHex) {
    for (const char* bad : {"", "#", "#12", "#12345", "#1234567", "#GGGGGG", "#12 456", "purple", "##FFFFFF"}) {
        EXPECT_FALSE(ui::Color::fromHex(bad).has_value()) << "'" << bad << "' should be rejected";
    }
}

TEST(ColorTest, HexRoundTripKeepsEveryChannel) {
    const ui::Color color(0x01, 0xAB, 0xFE, 0x7F);
    EXPECT_EQ(color.toHex(), "#01ABFE");
    EXPECT_EQ(color.toHex(true), "#01ABFE7F");
    EXPECT_EQ(ui::Color::fromHex(color.toHex(true)), color);
}

TEST(ColorTest, FromRgbAndArgbUseTheDocumentedLayouts) {
    EXPECT_EQ(ui::Color::fromRgb(0x7C3AED), ui::Color(0x7C, 0x3A, 0xED, 0xFF));
    EXPECT_EQ(ui::Color(0x11, 0x22, 0x33, 0x44).toArgb(), 0x44112233u);
}

TEST(ColorTest, MixLightenDarkenKeepAlphaAndClamp) {
    const ui::Color color(100, 100, 100, 200);

    EXPECT_EQ(color.mix(ui::Color(200, 0, 100, 200), 0.5f), ui::Color(150, 50, 100, 200));
    EXPECT_EQ(color.mix(ui::Color(0, 0, 0), -3.0f), color.mix(ui::Color(0, 0, 0), 0.0f));
    EXPECT_EQ(color.lighten(1.0f), ui::Color(255, 255, 255, 200));
    EXPECT_EQ(color.darken(1.0f), ui::Color(0, 0, 0, 200));
    EXPECT_EQ(color.withAlpha(5).a, 5);
}

TEST(ColorTest, LuminanceAndContrastMatchWcagReferenceValues) {
    EXPECT_FLOAT_EQ(ui::Color(0, 0, 0).luminance(), 0.0f);
    EXPECT_NEAR(ui::Color(255, 255, 255).luminance(), 1.0f, 1e-4f);
    EXPECT_NEAR(ui::Color::contrastRatio(ui::Color(0, 0, 0), ui::Color(255, 255, 255)), 21.0f, 0.01f);
    // Order must not matter.
    EXPECT_FLOAT_EQ(ui::Color::contrastRatio(ui::Color(255, 255, 255), ui::Color(119, 119, 119)),
                    ui::Color::contrastRatio(ui::Color(119, 119, 119), ui::Color(255, 255, 255)));
    // #777 on white is the classic ~4.48:1 example.
    EXPECT_NEAR(ui::Color::contrastRatio(ui::Color(255, 255, 255), ui::Color(119, 119, 119)), 4.48f, 0.05f);
}

TEST(ColorTest, HueOfPrimariesAndGreys) {
    EXPECT_FLOAT_EQ(ui::Color(255, 0, 0).hue(), 0.0f);
    EXPECT_FLOAT_EQ(ui::Color(0, 255, 0).hue(), 120.0f);
    EXPECT_FLOAT_EQ(ui::Color(0, 0, 255).hue(), 240.0f);
    EXPECT_FLOAT_EQ(ui::Color(128, 128, 128).hue(), 0.0f);
    EXPECT_NEAR(ui::Color(255, 0, 255).hue(), 300.0f, 0.01f);
}

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------

TEST(ThemeTest, DefaultIsBlackWithPurpleSecondaryColour) {
    const ui::Theme theme = ui::Theme::blackPurple();

    // Black base ...
    EXPECT_LT(theme.palette.background.luminance(), 0.01f);
    EXPECT_LT(theme.palette.surface.luminance(), 0.02f);

    // ... purple accents (violet hues sit between blue 240 and magenta 300).
    for (const ui::Color& purple : {theme.palette.accent, theme.palette.accentHover,
                                    theme.palette.accentActive, theme.palette.accentText}) {
        EXPECT_GE(purple.hue(), 250.0f) << purple.toHex();
        EXPECT_LE(purple.hue(), 290.0f) << purple.toHex();
    }
}

TEST(ThemeTest, DefaultPaletteStaysReadable) {
    const ui::ThemePalette& p = ui::Theme::blackPurple().palette;

    EXPECT_GE(ui::Color::contrastRatio(p.text, p.background), 7.0f);
    EXPECT_GE(ui::Color::contrastRatio(p.text, p.surface), 7.0f);
    EXPECT_GE(ui::Color::contrastRatio(p.textMuted, p.background), 4.5f);
    EXPECT_GE(ui::Color::contrastRatio(p.textMuted, p.surface), 4.5f);
    EXPECT_GE(ui::Color::contrastRatio(p.accentText, p.background), 4.5f);
    EXPECT_GE(ui::Color::contrastRatio(p.textOnAccent, p.accent), 4.5f);
    EXPECT_GE(ui::Color::contrastRatio(p.textOnAccent, p.accentActive), 4.5f);
    EXPECT_GE(ui::Color::contrastRatio(p.success, p.background), 4.5f);
    EXPECT_GE(ui::Color::contrastRatio(p.warning, p.background), 4.5f);
    EXPECT_GE(ui::Color::contrastRatio(p.danger, p.background), 4.5f);
}

TEST(ThemeTest, JsonRoundTripIsLossless) {
    ui::Theme original = ui::Theme::blackPurple();
    original.name = "Midnight";
    original.fontFile = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    original.palette.accent = ui::Color(1, 2, 3, 128);
    original.metrics.windowRounding = 3.5f;

    std::vector<ui::ThemeIssue> issues;
    const auto restored = ui::Theme::fromJson(original.toJson(), &issues);

    ASSERT_TRUE(restored.has_value());
    EXPECT_TRUE(issues.empty());
    EXPECT_EQ(*restored, original);
}

TEST(ThemeTest, PartialFileOverridesOnlyWhatItNames) {
    const auto json = nlohmann::json::parse(R"({"colors": {"background": "#000000"}})");

    const auto theme = ui::Theme::fromJson(json);

    ASSERT_TRUE(theme.has_value());
    EXPECT_EQ(theme->palette.background, ui::Color(0, 0, 0));
    EXPECT_EQ(theme->palette.accent, ui::Theme::blackPurple().palette.accent);
    EXPECT_EQ(theme->metrics, ui::Theme::blackPurple().metrics);
}

TEST(ThemeTest, AccentAloneDerivesHoverAndActiveShades) {
    const auto json = nlohmann::json::parse(R"({"colors": {"accent": "#FF00FF"}})");

    const auto theme = ui::Theme::fromJson(json);

    ASSERT_TRUE(theme.has_value());
    EXPECT_EQ(theme->palette.accent, ui::Color(255, 0, 255));
    EXPECT_GT(theme->palette.accentHover.luminance(), theme->palette.accent.luminance());
    EXPECT_LT(theme->palette.accentActive.luminance(), theme->palette.accent.luminance());
}

TEST(ThemeTest, ExplicitHoverShadeIsNotOverwrittenByDerivation) {
    const auto json = nlohmann::json::parse(R"({"colors": {"accent": "#FF00FF", "accentHover": "#00FF00"}})");

    const auto theme = ui::Theme::fromJson(json);

    ASSERT_TRUE(theme.has_value());
    EXPECT_EQ(theme->palette.accentHover, ui::Color(0, 255, 0));
}

TEST(ThemeTest, BadValuesAreReportedAndDefaultsKept) {
    const auto json = nlohmann::json::parse(R"({
        "name": 42,
        "fontFile": 12,
        "colors": {"accent": "not-a-colour", "text": 7, "sparkle": "#FFFFFF"},
        "metrics": {"windowRounding": 500, "fontSize": "big"},
        "extra": true
    })");
    std::vector<ui::ThemeIssue> issues;

    const auto theme = ui::Theme::fromJson(json, &issues);

    ASSERT_TRUE(theme.has_value());
    const ui::Theme defaults = ui::Theme::blackPurple();
    EXPECT_EQ(theme->name, defaults.name);
    EXPECT_EQ(theme->palette, defaults.palette);
    EXPECT_FLOAT_EQ(theme->metrics.windowRounding, 32.0f); // clamped to the documented maximum
    EXPECT_FLOAT_EQ(theme->metrics.fontSize, defaults.metrics.fontSize);

    std::vector<std::string> fields;
    for (const auto& issue : issues) fields.push_back(issue.field);
    for (const char* expected : {"name", "fontFile", "colors.accent", "colors.text", "colors.sparkle",
                                 "metrics.windowRounding", "metrics.fontSize", "extra"}) {
        EXPECT_NE(std::find(fields.begin(), fields.end(), expected), fields.end()) << expected;
    }
}

TEST(ThemeTest, FontFileDefaultsToAutomaticAndIsAlwaysWrittenOut) {
    EXPECT_TRUE(ui::Theme::blackPurple().fontFile.empty()) << "empty means: use the system UI font";

    const auto json = ui::Theme::blackPurple().toJson();
    ASSERT_TRUE(json.contains("fontFile")) << "kept in the file so users can discover it";
    EXPECT_EQ(json["fontFile"], "");

    const auto custom = ui::Theme::fromJson(nlohmann::json::parse(R"({"fontFile": "C:/Windows/Fonts/segoeui.ttf"})"));
    ASSERT_TRUE(custom.has_value());
    EXPECT_EQ(custom->fontFile, "C:/Windows/Fonts/segoeui.ttf");
}

TEST(ThemeTest, NonObjectDocumentIsRejected) {
    std::vector<ui::ThemeIssue> issues;
    EXPECT_FALSE(ui::Theme::fromJson(nlohmann::json::parse("[1,2,3]"), &issues).has_value());
    EXPECT_EQ(issues.size(), 1u);
}

// ---------------------------------------------------------------------------
// ThemeManager (Singleton + Observer)
// ---------------------------------------------------------------------------

namespace {

    class RecordingObserver : public ui::IThemeObserver {
    public:
        void onThemeChanged(const ui::Theme& theme) override {
            ++calls;
            last = theme;
        }

        int       calls = 0;
        ui::Theme last;
    };

    class ThemeManagerTest : public ::testing::Test {
    protected:
        void SetUp() override { ui::ThemeManager::getInstance().resetToDefault(); }
        void TearDown() override { ui::ThemeManager::getInstance().resetToDefault(); }

        static ui::Theme customTheme() {
            ui::Theme theme = ui::Theme::blackPurple();
            theme.name = "Test";
            theme.palette.accent = ui::Color(10, 20, 30);
            return theme;
        }
    };

}

TEST_F(ThemeManagerTest, IsASingleton) {
    EXPECT_EQ(&ui::ThemeManager::getInstance(), &ui::ThemeManager::getInstance());
}

TEST_F(ThemeManagerTest, StartsWithBlackPurple) {
    EXPECT_EQ(ui::ThemeManager::getInstance().getTheme(), ui::Theme::blackPurple());
}

TEST_F(ThemeManagerTest, SetThemeNotifiesObserversAndBumpsRevision) {
    auto& manager = ui::ThemeManager::getInstance();
    RecordingObserver observer;
    manager.addObserver(&observer);
    const auto before = manager.revision();

    manager.setTheme(customTheme());

    EXPECT_EQ(observer.calls, 1);
    EXPECT_EQ(observer.last, customTheme());
    EXPECT_EQ(manager.getTheme(), customTheme());
    EXPECT_EQ(manager.revision(), before + 1);
    manager.removeObserver(&observer);
}

TEST_F(ThemeManagerTest, SettingTheSameThemeIsSilent) {
    auto& manager = ui::ThemeManager::getInstance();
    RecordingObserver observer;
    manager.addObserver(&observer);
    const auto before = manager.revision();

    manager.setTheme(ui::Theme::blackPurple());

    EXPECT_EQ(observer.calls, 0);
    EXPECT_EQ(manager.revision(), before);
    manager.removeObserver(&observer);
}

TEST_F(ThemeManagerTest, RemovedObserversStopReceivingChanges) {
    auto& manager = ui::ThemeManager::getInstance();
    RecordingObserver observer;
    manager.addObserver(&observer);
    manager.removeObserver(&observer);

    manager.setTheme(customTheme());

    EXPECT_EQ(observer.calls, 0);
}

TEST_F(ThemeManagerTest, ObserverMayReadTheThemeFromItsCallback) {
    struct Reader : ui::IThemeObserver {
        void onThemeChanged(const ui::Theme&) override { seen = ui::ThemeManager::getInstance().getTheme(); }
        ui::Theme seen;
    } reader;
    auto& manager = ui::ThemeManager::getInstance();
    manager.addObserver(&reader);

    manager.setTheme(customTheme());

    EXPECT_EQ(reader.seen, customTheme());
    manager.removeObserver(&reader);
}

TEST_F(ThemeManagerTest, SaveThenLoadRestoresTheTheme) {
    test::TempDir dir;
    auto& manager = ui::ThemeManager::getInstance();
    manager.setTheme(customTheme());
    ASSERT_TRUE(manager.saveToFile(dir / "nested/theme.json")); // creates the parent directory

    manager.resetToDefault();
    ASSERT_NE(manager.getTheme(), customTheme());
    ASSERT_TRUE(manager.loadFromFile(dir / "nested/theme.json"));

    EXPECT_EQ(manager.getTheme(), customTheme());
    EXPECT_FALSE(std::filesystem::exists(dir / "nested/theme.json.tmp")) << "temp file must be renamed away";
}

TEST_F(ThemeManagerTest, UnreadableOrInvalidFilesLeaveTheThemeUntouched) {
    test::TempDir dir;
    auto& manager = ui::ThemeManager::getInstance();
    manager.setTheme(customTheme());

    std::vector<ui::ThemeIssue> issues;
    EXPECT_FALSE(manager.loadFromFile(dir / "missing.json", &issues));
    EXPECT_FALSE(issues.empty());

    {
        std::ofstream(dir / "broken.json") << "{ this is not json";
    }
    EXPECT_FALSE(manager.loadFromFile(dir / "broken.json"));

    {
        std::ofstream(dir / "array.json") << "[1, 2, 3]";
    }
    EXPECT_FALSE(manager.loadFromFile(dir / "array.json"));

    EXPECT_EQ(manager.getTheme(), customTheme());
}

TEST_F(ThemeManagerTest, PartialFileAppliesAndReportsIssues) {
    test::TempDir dir;
    {
        std::ofstream(dir / "theme.json") << R"({"colors": {"accent": "#FF00FF", "bogus": "#000000"}})";
    }
    auto& manager = ui::ThemeManager::getInstance();
    std::vector<ui::ThemeIssue> issues;

    ASSERT_TRUE(manager.loadFromFile(dir / "theme.json", &issues));

    EXPECT_EQ(manager.getTheme().palette.accent, ui::Color(255, 0, 255));
    ASSERT_EQ(issues.size(), 1u);
    EXPECT_EQ(issues[0].field, "colors.bogus");
}

TEST_F(ThemeManagerTest, ReloadIfChangedPicksUpEditsOnce) {
    test::TempDir dir;
    const auto file = dir / "theme.json";
    {
        std::ofstream(file) << R"({"colors": {"accent": "#FF0000"}})";
    }
    auto& manager = ui::ThemeManager::getInstance();
    ASSERT_TRUE(manager.loadFromFile(file));
    EXPECT_FALSE(manager.reloadIfChanged()) << "nothing changed yet";

    {
        std::ofstream(file) << R"({"colors": {"accent": "#00FF00"}})";
    }
    // Make the modification time unambiguously newer than what was recorded on load.
    std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) + std::chrono::seconds(5));

    EXPECT_TRUE(manager.reloadIfChanged());
    EXPECT_EQ(manager.getTheme().palette.accent, ui::Color(0, 255, 0));
    EXPECT_FALSE(manager.reloadIfChanged()) << "a second poll must not reload again";
}

TEST_F(ThemeManagerTest, ReloadWithoutAPriorLoadDoesNothing) {
    EXPECT_FALSE(ui::ThemeManager::getInstance().reloadIfChanged());
}
