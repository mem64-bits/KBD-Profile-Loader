#include <gtest/gtest.h>
#include "files/IniConfig.h"

#if defined(_WIN32)
#include <windows.h>
#endif

TEST(IniConfigTest, ValidHotkeyParsing) {
#if defined(_WIN32)
    auto result = Files::IniConfig::parseHotkey("Ctrl+Shift+]");
    EXPECT_TRUE(result.isValid);
    EXPECT_EQ(result.binding.vkCode, VK_OEM_6); // ']'
    EXPECT_TRUE(result.binding.modifiers & MOD_CONTROL);
    EXPECT_TRUE(result.binding.modifiers & MOD_SHIFT);

    auto resultFKey = Files::IniConfig::parseHotkey("Ctrl+Alt+F9");
    EXPECT_TRUE(resultFKey.isValid);
    EXPECT_EQ(resultFKey.binding.vkCode, VK_F9);
    EXPECT_TRUE(resultFKey.binding.modifiers & MOD_CONTROL);
    EXPECT_TRUE(resultFKey.binding.modifiers & MOD_ALT);
#else
    auto result = Files::IniConfig::parseHotkey("Ctrl+Shift+]");
    EXPECT_TRUE(result.isValid);
#endif
}

TEST(IniConfigTest, CatchesModifierTyposGracefully) {
#if defined(_WIN32)
    // Typo: 'Crtl' instead of 'Ctrl'
    auto result = Files::IniConfig::parseHotkey("Crtl+Shift+]");
    EXPECT_FALSE(result.isValid);
    EXPECT_NE(result.errorMessage.find("Unrecognized"), std::string::npos);

    // Missing base key (modifiers only)
    auto resultNoKey = Files::IniConfig::parseHotkey("Ctrl+Shift+");
    EXPECT_FALSE(resultNoKey.isValid);
    EXPECT_NE(resultNoKey.errorMessage.find("No base key"), std::string::npos);

    // Empty string
    auto resultEmpty = Files::IniConfig::parseHotkey("");
    EXPECT_FALSE(resultEmpty.isValid);
#else
    SUCCEED();
#endif
}

TEST(IniConfigTest, DefaultConfigPopulatesWhenMissing) {
    Files::IniConfig config;
    // Loading non-existent path triggers default creation
    fs::path tempPath = fs::temp_directory_path() / "test_kbd_config.ini";
    if (fs::exists(tempPath)) fs::remove(tempPath);

    EXPECT_TRUE(config.load(tempPath));
    EXPECT_EQ(config.get("Hotkeys", "next_profile"), "Ctrl+Shift+]");
    EXPECT_EQ(config.get("Hotkeys", "prev_profile"), "Ctrl+Shift+[");

    fs::remove(tempPath);
}