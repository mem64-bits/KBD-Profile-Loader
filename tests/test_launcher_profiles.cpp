#include <gtest/gtest.h>

#include "files/ProfileService.h"
#include "kbd/KeyboardDevice.h"
#include "kbd/KeychronProtocol.h"
#include "kbd/ProfileMemory.h"
#include "support/TempDir.h"
#include "ui/ActuationModel.h"
#include "ui/ProfileServiceGateway.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

// Real profiles exported by the Keychron Launcher (tests/fixtures/launcher): the format this app has to read.

namespace {

    using kbd::KeyTravelConfig;
    using kbd::ProfileMemory;

    constexpr const char* STANDARD = "HEProfile-Standard-2026-10-28.json";
    constexpr const char* TEKKEN   = "HEProfile-Tekken 8-2026-10-28.json";
    constexpr const char* GAMEPAD  = "HEProfile-Gamepad-2026-10-28.json";

    std::filesystem::path fixtureDir() {
        return std::filesystem::path(__FILE__).parent_path() / "fixtures" / "launcher";
    }

    nlohmann::json load(const char* name) {
        std::ifstream file(fixtureDir() / name);
        return nlohmann::json::parse(file);
    }

    std::array<std::uint8_t, ProfileMemory::PROFILE_SIZE> memoryOf(const char* name) {
        return kbd::KeychronProtocol::extractMemoryBuffer(load(name));
    }

    KeyTravelConfig key(const std::array<std::uint8_t, ProfileMemory::PROFILE_SIZE>& memory, int row, int col) {
        return ProfileMemory::decodeKeyTravel(memory, static_cast<std::size_t>(row), static_cast<std::size_t>(col)).value();
    }

    int countOccurrences(const std::string& text, const std::string& part) {
        int count = 0;
        for (auto pos = text.find(part); pos != std::string::npos; pos = text.find(part, pos + part.size())) ++count;
        return count;
    }

} // namespace

TEST(LauncherProfileTest, FixturesArePresent) {
    for (const char* name : {STANDARD, TEKKEN, GAMEPAD}) {
        EXPECT_TRUE(std::filesystem::exists(fixtureDir() / name)) << name;
    }
}

TEST(LauncherProfileTest, TekkenMovementAndAttackKeysKeepTheirOwnSettings) {
    const auto memory = memoryOf(TEKKEN);

    EXPECT_EQ(ProfileMemory::decodeGlobalTravel(memory), (kbd::GlobalTravelConfig{2, 20, 4, 4, 0, 0}));
    for (const auto& [r, c] : {std::pair{2, 2}, std::pair{3, 1}, std::pair{3, 2}, std::pair{3, 3}}) { // W A S D
        EXPECT_EQ(key(memory, r, c), (KeyTravelConfig{2, 3, 1, 1, 0, 0})) << r << "," << c;
    }
    EXPECT_EQ(key(memory, 5, 6), (KeyTravelConfig{2, 4, 1, 1, 0, 0}));  // Space
    EXPECT_EQ(key(memory, 2, 7), (KeyTravelConfig{2, 7, 2, 2, 0, 0}));  // U
    EXPECT_EQ(key(memory, 0, 0), (KeyTravelConfig{1, 20, 4, 4, 0, 0})); // Esc

    const auto keys = ui::actuation::keyMatrixFromProfile(memory);
    ASSERT_TRUE(keys);
    EXPECT_EQ(keys->overrideCount(), 85u) << "every key on the board has its own settings; unused positions do not count";
}

TEST(LauncherProfileTest, TekkenSnapTapPairsBecomeSocdEntries) {
    const auto memory = memoryOf(TEKKEN);

    EXPECT_FALSE(ProfileMemory::decodeSocd(memory, 0)->isSet());
    EXPECT_EQ(ProfileMemory::decodeSocd(memory, 1), (kbd::SocdPair{3, 1, 3, 3, 6})); // A / D, neutral
    EXPECT_EQ(ProfileMemory::decodeSocd(memory, 2), (kbd::SocdPair{5, 6, 3, 2, 6})); // Space / S
    EXPECT_EQ(ProfileMemory::decodeSocd(memory, 3), (kbd::SocdPair{2, 2, 3, 2, 6})); // W / S
    for (std::size_t i = 4; i < ProfileMemory::SOCD_COUNT; ++i) EXPECT_FALSE(ProfileMemory::decodeSocd(memory, i)->isSet()) << i;
}

TEST(LauncherProfileTest, KeysThatFollowTheSlotUseTheRawValuesNotTheDisplayedOnes) {
    // In the export the unused position (1, 15) shows distance 20 / 0.4 / 0.4 (the slot's values, for display)
    // while its "edit" copy holds what the keyboard stores: 0 = follow the slot.
    const auto json = load(STANDARD);
    ASSERT_EQ(json["keyMatrix"][1][15]["distance"], 20);
    ASSERT_EQ(json["keyMatrix"][1][15]["edit"]["distance"], 0);

    const auto memory = memoryOf(STANDARD);
    EXPECT_EQ(key(memory, 1, 15), (KeyTravelConfig{0, 0, 0, 0, 0, 0}));
    EXPECT_EQ(key(memory, 0, 1), (KeyTravelConfig{1, 22, 5, 5, 0, 0})); // F1
    EXPECT_EQ(key(memory, 4, 0), (KeyTravelConfig{1, 20, 1, 1, 0, 0})); // left Shift
    EXPECT_EQ(ui::actuation::keyMatrixFromProfile(memory)->overrideCount(), 85u);
}

TEST(LauncherProfileTest, GamepadKeysKeepTheirAxisAndFollowTheSlotOtherwise) {
    const auto memory = memoryOf(GAMEPAD);

    EXPECT_EQ(key(memory, 3, 1), (KeyTravelConfig{0, 0, 0, 0, kbd::AdvancedMode::GAMEPAD, 0})); // A -> axis 0
    EXPECT_EQ(key(memory, 3, 3), (KeyTravelConfig{0, 0, 0, 0, kbd::AdvancedMode::GAMEPAD, 1})); // D -> axis 1
    int gamepadKeys = 0;
    for (int r = 0; r < 6; ++r) {
        for (int c = 0; c < 16; ++c) gamepadKeys += key(memory, r, c).advancedMode == kbd::AdvancedMode::GAMEPAD;
    }
    EXPECT_EQ(gamepadKeys, 25);
    EXPECT_EQ(ui::actuation::keyMatrixFromProfile(memory)->overrideCount(), 0u);
}

TEST(LauncherProfileTest, NamesComeFromTheFileName) {
    EXPECT_EQ(kbd::ProfileService::displayNameFor(TEKKEN), "Tekken 8");
    EXPECT_EQ(kbd::ProfileService::displayNameFor("/x/HEProfile-Standard-2026-10-28.json"), "Standard");
    EXPECT_EQ(kbd::ProfileService::displayNameFor("my-profile.json"), "my-profile");
    EXPECT_EQ(kbd::ProfileService::displayNameFor("HEProfile-Valorant.json"), "Valorant");
    EXPECT_EQ(kbd::ProfileService::displayNameFor("HEProfile-.json"), "HEProfile-");
}

// ---------------------------------------------------------------------------
// Loaded and written through the real service / gateway (dry-run keyboard)
// ---------------------------------------------------------------------------

namespace {

    class LauncherGatewayTest : public ::testing::Test {
    protected:
        void SetUp() override {
            for (const char* name : {STANDARD, TEKKEN, GAMEPAD}) std::filesystem::copy_file(fixtureDir() / name, dir / name);
            keyboard.setDryRun(true, (dir / "dump.txt").string());
            service.loadAllFromDirectory(dir.path());
        }

        std::string dump() const {
            std::ifstream file(dir.path() / "dump.txt");
            return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }

        test::TempDir             dir;
        kbd::KeyboardDevice       keyboard;
        kbd::ProfileService       service{keyboard};
        ui::ProfileServiceGateway gateway{service, keyboard};
    };

} // namespace

TEST_F(LauncherGatewayTest, TheThreeExportsFillTheThreeSlots) {
    const auto slots = gateway.describeSlots();
    EXPECT_EQ(slots[0].name, "Standard");
    EXPECT_EQ(slots[1].name, "Tekken 8");
    EXPECT_EQ(slots[2].name, "Gamepad");

    const auto library = gateway.describeLibrary();
    ASSERT_EQ(library.size(), 3u);
    EXPECT_EQ(library[1].extras.socdPairs, 3u);
    EXPECT_EQ(library[2].extras.gamepadKeys, 25u);
    EXPECT_EQ(library[0].keyOverrides, 85u);
}

TEST_F(LauncherGatewayTest, WritingTekkenSendsItsSocdPairsAndClearsTheRest) {
    const auto result = gateway.flashSlotProfile(1);

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_NE(result.message.find("3 SOCD pairs"), std::string::npos) << result.message;
    const std::string sent = dump();
    // 0xA9 0x16 <slot> <row1> <col1> <row2> <col2> <index> <type>
    EXPECT_NE(sent.find("0xa9 0x16 0x01 0x03 0x01 0x03 0x03 0x01 0x06"), std::string::npos) << "A / D";
    EXPECT_NE(sent.find("0xa9 0x16 0x01 0x05 0x06 0x03 0x02 0x02 0x06"), std::string::npos) << "Space / S";
    EXPECT_NE(sent.find("0xa9 0x16 0x01 0x02 0x02 0x03 0x02 0x03 0x06"), std::string::npos) << "W / S";
    EXPECT_EQ(countOccurrences(sent, "0xa9 0x16 0x01"), 20) << "all 20 entries, the unused ones cleared";
    EXPECT_EQ(countOccurrences(sent, "0xa9 0x15 0x01 0x00"), 85) << "no advanced keys: every key on the board is cleared";
}

TEST_F(LauncherGatewayTest, WritingTheGamepadProfileMapsItsAxes) {
    const auto result = gateway.flashSlotProfile(2);

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_NE(result.message.find("25 gamepad keys"), std::string::npos) << result.message;
    const std::string sent = dump();
    // 0xA9 0x15 <slot> <ADV_MODE_GAME_CONTROLLER = 2> <row> <col> <axis>
    EXPECT_NE(sent.find("0xa9 0x15 0x02 0x02 0x03 0x01 0x00"), std::string::npos) << "A -> axis 0";
    EXPECT_NE(sent.find("0xa9 0x15 0x02 0x02 0x03 0x03 0x01"), std::string::npos) << "D -> axis 1";
    EXPECT_EQ(countOccurrences(sent, "0xa9 0x15 0x02 0x02"), 25);
    EXPECT_EQ(countOccurrences(sent, "0xa9 0x15 0x02 0x00"), 60);
}

TEST_F(LauncherGatewayTest, TwoExportsOfTheSameProfileBothStayListed) {
    std::filesystem::copy_file(fixtureDir() / TEKKEN, dir / "HEProfile-Tekken 8-2026-11-04.json");
    auto copy = load(TEKKEN);
    copy.erase("current");
    std::ofstream(dir / "HEProfile-Tekken 8-2026-11-04.json") << copy.dump();
    gateway.handleFileCreated(dir / "HEProfile-Tekken 8-2026-11-04.json");

    const auto library = gateway.describeLibrary();
    ASSERT_EQ(library.size(), 4u) << "same name, different files";
    EXPECT_EQ(library[3].name, "Tekken 8");
    EXPECT_FALSE(library[3].assignedSlot.has_value());
    EXPECT_EQ(gateway.describeSlots()[1].fileName, TEKKEN);
}
