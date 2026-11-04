#include <gtest/gtest.h>

#include "files/ProfileService.h"
#include "kbd/KeyboardDevice.h"
#include "kbd/ProfileMemory.h"
#include "support/TempDir.h"
#include "ui/ProfileServiceGateway.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iomanip>
#include <sstream>

// Exercises the real kbd::ProfileService + ui::ProfileServiceGateway against a dry-run KeyboardDevice,
// which writes every packet it would have sent to a text file instead of touching the USB bus.

namespace {

    using kbd::GlobalTravelConfig;

    std::string hexOf(const std::vector<std::uint8_t>& bytes) {
        std::ostringstream out;
        for (const auto b : bytes) out << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
        return out.str();
    }

    // A profile file the way the daemon expects it: a name, the slot it belongs to and the 860-byte memory dump.
    void writeProfile(const std::filesystem::path& path, const std::string& name, const int slot,
                      const GlobalTravelConfig& travel, const bool asHexString) {
        std::vector<std::uint8_t> memory(860, 0);
        const auto head = kbd::ProfileMemory::encodeGlobalTravel(travel);
        std::copy(head.begin(), head.end(), memory.begin());

        nlohmann::json json;
        json["name"] = name;
        json["current"] = slot;
        if (asHexString) {
            json["raw_hex"] = hexOf(memory);
        } else {
            json["memory"] = memory;
        }
        std::ofstream(path) << json.dump(2);
    }

    class GatewayTest : public ::testing::Test {
    protected:
        void SetUp() override {
            writeProfile(dir / "default.json", "Default", 0, {GlobalTravelConfig::MODE_REGULAR, 20, 4, 4, 0, 0}, false);
            writeProfile(dir / "tekken.json", "Tekken 8", 1, {GlobalTravelConfig::MODE_RAPID, 3, 1, 1, 0, 0}, true);
            keyboard.setDryRun(true, (dir / "dump.txt").string());
            service.loadAllFromDirectory(dir.path());
        }

        // Everything the dry-run device was asked to send, one packet per line.
        std::string dump() const {
            std::ifstream file(dir.path() / "dump.txt");
            return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }

        static int countOccurrences(const std::string& text, const std::string& part) {
            int count = 0;
            for (auto pos = text.find(part); pos != std::string::npos; pos = text.find(part, pos + part.size())) ++count;
            return count;
        }

        test::TempDir          dir;
        kbd::KeyboardDevice    keyboard;
        kbd::ProfileService    service{keyboard};
        ui::ProfileServiceGateway gateway{service, keyboard};
    };

} // namespace

TEST_F(GatewayTest, DescribesSlotsFromTheProfileFiles) {
    const auto slots = gateway.describeSlots();

    EXPECT_TRUE(slots[0].hasProfile);
    EXPECT_EQ(slots[0].name, "Default");
    EXPECT_EQ(slots[0].fileName, "default.json");
    ASSERT_TRUE(slots[0].storedSettings.has_value());
    EXPECT_EQ(*slots[0].storedSettings, (ui::ActuationSettings{false, 20, 4, 4}));

    EXPECT_TRUE(slots[1].hasProfile);
    EXPECT_EQ(slots[1].name, "Tekken 8");
    ASSERT_TRUE(slots[1].storedSettings.has_value()) << "raw_hex dumps must decode as well";
    EXPECT_EQ(*slots[1].storedSettings, (ui::ActuationSettings{true, 3, 1, 1}));

    EXPECT_FALSE(slots[2].hasProfile);
    EXPECT_EQ(slots[2].name, "Slot 3");
    EXPECT_FALSE(slots[2].storedSettings.has_value());
}

TEST_F(GatewayTest, ProfileWithoutAMemoryDumpStillShowsUpWithoutSettings) {
    std::ofstream(dir / "bare.json") << R"({"name": "Bare", "current": 2})";
    gateway.handleFileCreated(dir / "bare.json");

    const auto slots = gateway.describeSlots();

    EXPECT_TRUE(slots[2].hasProfile);
    EXPECT_EQ(slots[2].name, "Bare");
    EXPECT_FALSE(slots[2].storedSettings.has_value());
}

TEST_F(GatewayTest, FileEventsReachTheProfileService) {
    writeProfile(dir / "gamepad.json", "Gamepad", 2, {GlobalTravelConfig::MODE_REGULAR, 14, 4, 4, 0, 0}, false);

    gateway.handleFileCreated(dir / "gamepad.json");
    EXPECT_TRUE(gateway.describeSlots()[2].hasProfile);

    gateway.handleFileDeleted(dir / "gamepad.json");
    EXPECT_FALSE(gateway.describeSlots()[2].hasProfile);
}

TEST_F(GatewayTest, DryRunDeviceReportsAsConnected) {
    const auto device = gateway.describeDevice();

    EXPECT_TRUE(device.connected);
    EXPECT_EQ(device.transport, kbd::TransportMode::Wired);
}

TEST_F(GatewayTest, ApplyActuationSendsTheFastTravelPacket) {
    const auto result = gateway.applyActuation(1, ui::ActuationSettings{true, 3, 1, 1});

    EXPECT_TRUE(result.ok) << result.message;
    // 0xA9 0x14 <slot> <mode: 2 = Rapid Trigger> <act> <press> <release> <entire keyboard>
    EXPECT_NE(dump().find("0xa9 0x14 0x01 0x02 0x03 0x01 0x01 0x01"), std::string::npos) << dump();
}

TEST_F(GatewayTest, ApplyActuationClampsOutOfRangeValues) {
    const auto result = gateway.applyActuation(2, ui::ActuationSettings{false, 0, 200, 0});

    EXPECT_TRUE(result.ok) << result.message;
    // mode 1 = regular, act 0 -> 1, press 200 -> 39 (0x27), release 0 -> 1
    EXPECT_NE(dump().find("0xa9 0x14 0x02 0x01 0x01 0x27 0x01 0x01"), std::string::npos) << dump();
}

TEST_F(GatewayTest, EditingTheActiveSlotHopsToANeighbourAndBackSoTheFirmwareReloadsIt) {
    // The dry-run keyboard always reports slot 0 as active, so applying to slot 0 edits the active slot.
    const auto result = gateway.applyActuation(0, ui::ActuationSettings{false, 10, 4, 4});

    EXPECT_TRUE(result.ok) << result.message;
    const std::string sent = dump();
    const auto apply = sent.find("0xa9 0x14 0x00");
    const auto hop   = sent.find("0xa9 0x11 0x01");
    const auto back  = sent.find("0xa9 0x11 0x00");
    ASSERT_NE(apply, std::string::npos);
    ASSERT_NE(hop, std::string::npos);
    ASSERT_NE(back, std::string::npos);
    EXPECT_LT(apply, hop);
    EXPECT_LT(hop, back);
}

TEST_F(GatewayTest, EditingAnInactiveSlotDoesNotDisturbTheActiveOne) {
    const auto result = gateway.applyActuation(2, ui::ActuationSettings{false, 10, 4, 4});

    EXPECT_TRUE(result.ok) << result.message;
    EXPECT_EQ(countOccurrences(dump(), "0xa9 0x11"), 0) << "no slot selection expected";
}

TEST_F(GatewayTest, SaveSlotSendsTheFlashCommitPacket) {
    const auto result = gateway.saveSlot(2);

    EXPECT_TRUE(result.ok) << result.message;
    EXPECT_NE(dump().find("0xa9 0x1f 0x02"), std::string::npos);
}

TEST_F(GatewayTest, WritingAProfileReplaysTheSettersAndCommits) {
    const auto result = gateway.flashSlotProfile(0);

    EXPECT_TRUE(result.ok) << result.message;
    const std::string sent = dump();
    // 1. global settings, whole keyboard: regular 2.0 mm, 0.4 / 0.4 mm
    const auto global = sent.find("0xa9 0x14 0x00 0x01 0x14 0x04 0x04 0x01");
    // 2. every key follows the slot: mode 0, values 0, all 16 columns of all 6 rows selected
    const auto keys = sent.find("0xa9 0x14 0x00 0x00 0x00 0x00 0x00 0x00 0xff 0xff 0x00 0xff 0xff 0x00");
    // 3. name, 4. commit
    const auto name = sent.find("0xa9 0x13 0x00 0x07");
    const auto save = sent.find("0xa9 0x1f 0x00");
    ASSERT_NE(global, std::string::npos) << sent;
    ASSERT_NE(keys, std::string::npos) << sent;
    ASSERT_NE(name, std::string::npos) << sent;
    ASSERT_NE(save, std::string::npos) << sent;
    EXPECT_LT(global, keys);
    EXPECT_LT(keys, name);
    EXPECT_LT(name, save);
    EXPECT_EQ(countOccurrences(sent, "0xa9 0x12"), 0) << "0x12 only reads; it must not be used to write";
}

TEST_F(GatewayTest, WritingALauncherProfileSendsOnePacketPerGroupOfKeys) {
    nlohmann::json profile;
    profile["name"]    = "Tekken 8";
    profile["current"] = 1;
    profile["global"]  = {{"mode", 2}, {"distance", 3}, {"advance_press", 1}, {"advance_release", 1}};
    profile["keyMatrix"] = nlohmann::json::array();
    for (int r = 0; r < 6; ++r) {
        nlohmann::json row = nlohmann::json::array();
        for (int c = 0; c < 16; ++c) row.push_back({{"mode", 0}, {"distance", 0}, {"advance_press", 0}, {"advance_release", 0}});
        profile["keyMatrix"].push_back(row);
    }
    for (const auto& [r, c] : {std::pair{2, 2}, std::pair{3, 1}, std::pair{3, 2}, std::pair{3, 3}}) { // W A S D
        profile["keyMatrix"][r][c] = {{"mode", 2}, {"distance", 2}, {"advance_press", 1}, {"advance_release", 1}};
    }
    std::ofstream(dir / "tekken.json") << profile.dump(2);
    gateway.handleFileDeleted(dir / "tekken.json");
    gateway.handleFileCreated(dir / "tekken.json");

    const auto result = gateway.flashSlotProfile(1);

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_NE(result.message.find("4 keys with their own settings"), std::string::npos) << result.message;
    const std::string sent = dump();
    // WASD: Rapid Trigger 0.2 mm, 0.1 / 0.1; masks row 2 = bit 2 (0x04), row 3 = bits 1-3 (0x0e)
    EXPECT_NE(sent.find("0xa9 0x14 0x01 0x02 0x02 0x01 0x01 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x04 0x00 0x00 0x0e 0x00 0x00"),
              std::string::npos) << sent;
    // The other 92 keys follow the slot: row 2 without W = 0xfffb, row 3 without A S D = 0xfff1
    EXPECT_NE(sent.find("0xa9 0x14 0x01 0x00 0x00 0x00 0x00 0x00 0xff 0xff 0x00 0xff 0xff 0x00 0xfb 0xff 0x00 0xf1 0xff 0x00"),
              std::string::npos) << sent;
    EXPECT_EQ(countOccurrences(sent, "0xa9 0x14 0x01"), 3) << "global + two key groups";
}

TEST_F(GatewayTest, ApplyKeyActuationSendsOneMaskedPacket) {
    const auto result = gateway.applyKeyActuation(2, {{2, 2}, {3, 1}}, ui::KeyActuation{ui::KeyActuation::MODE_RAPID, 3, 1, 2});

    EXPECT_TRUE(result.ok) << result.message;
    EXPECT_NE(dump().find("0xa9 0x14 0x02 0x02 0x03 0x01 0x02 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x04 0x00 0x00 0x02 0x00 0x00"),
              std::string::npos) << dump();
    EXPECT_EQ(countOccurrences(dump(), "0xa9 0x11"), 0) << "per-key values apply at once; no slot hop needed";
}

TEST_F(GatewayTest, ApplyKeyActuationClampsAndNeedsKeys) {
    EXPECT_FALSE(gateway.applyKeyActuation(0, {}, ui::KeyActuation{}).ok);
    EXPECT_FALSE(gateway.applyKeyActuation(0, {{7, 3}}, ui::KeyActuation{}).ok) << "outside the matrix";

    const auto result = gateway.applyKeyActuation(0, {{0, 0}}, ui::KeyActuation{9, 99, 0, 50});
    EXPECT_TRUE(result.ok) << result.message;
    // mode 9 -> 2, act 99 -> 39 (0x27), press 0 stays 0 (= global), release 50 -> 39
    EXPECT_NE(dump().find("0xa9 0x14 0x00 0x02 0x27 0x00 0x27 0x00 0x01 0x00 0x00"), std::string::npos) << dump();
}

TEST_F(GatewayTest, KeyMatrixReadBackIsUnavailableWithoutRealHardware) {
    EXPECT_FALSE(gateway.readKeyMatrix(0).has_value());
}

// ---------------------------------------------------------------------------
// Profile library
// ---------------------------------------------------------------------------

namespace {

    nlohmann::json readJson(const std::filesystem::path& path) {
        std::ifstream file(path);
        return nlohmann::json::parse(file);
    }

} // namespace

TEST_F(GatewayTest, LibraryListsEveryFileWithItsSlot) {
    writeProfile(dir / "spare.json", "Spare", 0, {GlobalTravelConfig::MODE_REGULAR, 14, 4, 4, 0, 0}, false);
    auto spare = readJson(dir / "spare.json");
    spare.erase("current");
    std::ofstream(dir / "spare.json") << spare.dump();
    gateway.handleFileCreated(dir / "spare.json");

    const auto library = gateway.describeLibrary();

    ASSERT_EQ(library.size(), 3u);
    EXPECT_EQ(library[0].fileName, "default.json");
    EXPECT_EQ(library[0].assignedSlot, std::optional<std::uint8_t>(0));
    EXPECT_EQ(library[1].assignedSlot, std::optional<std::uint8_t>(1));
    EXPECT_EQ(library[2].fileName, "spare.json");
    EXPECT_FALSE(library[2].assignedSlot.has_value()) << "no \"current\" field = library only";
    EXPECT_FALSE(gateway.describeSlots()[2].hasProfile) << "a library-only file must not show up in a slot";
}

TEST_F(GatewayTest, AssigningASlotTakesItAwayFromTheFileThatHadIt) {
    const auto result = gateway.assignProfileToSlot("tekken.json", 0, false);

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(readJson(dir / "tekken.json")["current"], 0);
    EXPECT_FALSE(readJson(dir / "default.json").contains("current")) << "one profile per slot";

    const auto slots = gateway.describeSlots();
    EXPECT_EQ(slots[0].name, "Tekken 8") << "the in-memory profile is updated, not just the file";
    EXPECT_FALSE(slots[1].hasProfile);

    const auto library = gateway.describeLibrary();
    ASSERT_EQ(library.size(), 2u);
    EXPECT_EQ(library[0].fileName, "default.json") << "assigning keeps the library order";
    EXPECT_FALSE(library[0].assignedSlot.has_value());
    EXPECT_TRUE(dump().empty()) << "assigning alone does not talk to the keyboard";
}

TEST_F(GatewayTest, AssignAndWriteSendsTheProfile) {
    const auto result = gateway.assignProfileToSlot("default.json", 2, true);

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_NE(dump().find("0xa9 0x14 0x02 0x01 0x14"), std::string::npos) << dump();
    EXPECT_NE(dump().find("0xa9 0x1f 0x02"), std::string::npos) << dump();
}

TEST_F(GatewayTest, AssigningAnUnknownFileFails) {
    EXPECT_FALSE(gateway.assignProfileToSlot("nope.json", 1, false).ok);
}

TEST_F(GatewayTest, ClearingASlotKeepsTheFileInTheLibrary) {
    ASSERT_TRUE(gateway.clearSlot(1).ok);

    EXPECT_FALSE(gateway.describeSlots()[1].hasProfile);
    EXPECT_FALSE(readJson(dir / "tekken.json").contains("current"));
    ASSERT_EQ(gateway.describeLibrary().size(), 2u);
    EXPECT_FALSE(gateway.clearSlot(1).ok) << "nothing left to clear";
}

TEST_F(GatewayTest, ReorderingMovesProfilesInTheLibrary) {
    ASSERT_TRUE(gateway.reorderLibrary(1, 0).ok);
    EXPECT_EQ(gateway.describeLibrary()[0].fileName, "tekken.json");
    EXPECT_FALSE(gateway.reorderLibrary(0, 5).ok);
}

TEST_F(GatewayTest, CyclingSkipsProfilesWithoutASlot) {
    std::ofstream(dir / "spare.json") << R"({"name": "Spare", "memory": [81, 4, 1, 0]})";
    gateway.handleFileCreated(dir / "spare.json");
    ASSERT_TRUE(gateway.reorderLibrary(2, 1).ok); // Default, Spare, Tekken 8

    (void)gateway.cycleProfile(true);

    EXPECT_EQ(countOccurrences(dump(), "0xa9 0x11"), 1) << dump();
    EXPECT_NE(dump().find("0xa9 0x11 0x01"), std::string::npos) << "went past Spare to Tekken 8 (Slot 2)";
}

TEST_F(GatewayTest, FlashingAnEmptySlotIsRefused) {
    const auto result = gateway.flashSlotProfile(2);

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.message.find("No profile file is assigned to Slot 3"), std::string::npos);
    EXPECT_TRUE(dump().empty());
}

TEST_F(GatewayTest, ReadBackIsUnavailableWithoutRealHardware) {
    EXPECT_FALSE(gateway.readActuation(0, std::nullopt).has_value());
}

TEST_F(GatewayTest, CyclingWithoutProfilesFailsCleanly) {
    for (const auto& file : {"default.json", "tekken.json"}) {
        gateway.handleFileDeleted(dir / file);
    }

    const auto result = gateway.cycleProfile(true);

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.message.find("No profile files"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Advanced keys, dynamic keystrokes, game controller
// ---------------------------------------------------------------------------

TEST_F(GatewayTest, WritingAProfileSendsDynamicKeystrokesWithTheirEntry) {
    std::vector<std::uint8_t> memory(860, 0);
    const auto head = kbd::ProfileMemory::encodeGlobalTravel({GlobalTravelConfig::MODE_REGULAR, 20, 4, 4, 0, 0});
    std::copy(head.begin(), head.end(), memory.begin());
    kbd::KeyTravelConfig dks{0, 0, 0, 0, kbd::AdvancedMode::DKS, 0};
    ASSERT_TRUE(kbd::ProfileMemory::encodeKeyTravel(memory, 2, 2, dks)); // W: entry 0
    ASSERT_TRUE(kbd::ProfileMemory::encodeOkmc(memory, 0, kbd::deepPressCombo(kbd::Keycode::W, kbd::Keycode::LEFT_SHIFT, 10, 30)));
    dks.advancedData = 5;
    ASSERT_TRUE(kbd::ProfileMemory::encodeKeyTravel(memory, 2, 3, dks)); // E: entry 5 has no keycodes
    nlohmann::json json;
    json["name"] = "Sprint";
    json["current"] = 2;
    json["memory"] = memory;
    std::ofstream(dir / "sprint.json") << json.dump(2);
    service.loadProfileFromFile(dir / "sprint.json");

    const auto result = gateway.flashSlotProfile(2);
    ASSERT_TRUE(result.ok) << result.message;
    const std::string sent = dump();
    EXPECT_NE(sent.find("0xa9 0x15 0x02 0x01 0x02 0x02 0x00 0x0a 0x0a 0x1e 0x1b 0x1a 0x00 0xe1 0x00"), std::string::npos) << sent;
    EXPECT_NE(sent.find("0xa9 0x15 0x02 0x00 0x02 0x03 0x00"), std::string::npos) << "an empty entry becomes a plain key";
    EXPECT_NE(result.message.find("1 dynamic keystrokes"), std::string::npos) << result.message;
    EXPECT_NE(result.message.find("had no keycodes"), std::string::npos) << result.message;
}

TEST_F(GatewayTest, AdvancedKeyChangesBecomeOneSetAdvanceModePacketEach) {
    ui::AdvancedKeyChange change;
    change.key   = {2, 2};
    change.kind  = ui::AdvancedKeyChange::Kind::Gamepad;
    change.index = kbd::GamepadTarget::LEFT_STICK_UP;
    ASSERT_TRUE(gateway.applyAdvancedKey(1, change).ok);
    EXPECT_NE(dump().find("0xa9 0x15 0x01 0x02 0x02 0x02 0x03"), std::string::npos) << dump();

    change.index = 12; // GC_MAX: not a target
    EXPECT_FALSE(gateway.applyAdvancedKey(1, change).ok);
    change.kind = ui::AdvancedKeyChange::Kind::DynamicKeystroke;
    change.index = 0;
    EXPECT_FALSE(gateway.applyAdvancedKey(1, change).ok) << "a keystroke without keycodes is refused";
    EXPECT_EQ(countOccurrences(dump(), "0xa9 0x15"), 1);

    ASSERT_TRUE(gateway.applySocd(1, 4, kbd::SocdPair{3, 1, 3, 3, 3}).ok);
    EXPECT_NE(dump().find("0xa9 0x16 0x01 0x03 0x01 0x03 0x03 0x04 0x03"), std::string::npos) << dump();
}

TEST_F(GatewayTest, ControllerSettingsGoToTheKeyboardAndValidateTheCurve) {
    ASSERT_TRUE(gateway.applyCurve(kbd::DEFAULT_CURVE).ok);
    EXPECT_NE(dump().find("0xa9 0x21 0x00 0x00 0x0a 0x1f 0x1e 0x5f 0x28 0x7f"), std::string::npos) << dump();
    EXPECT_FALSE(gateway.applyCurve({{{0, 0}, {20, 31}, {10, 95}, {40, 127}}}).ok);
    ASSERT_TRUE(gateway.applyControllerMode(kbd::ControllerMode::XINPUT | kbd::ControllerMode::TYPING).ok);
    EXPECT_NE(dump().find("0xa9 0x23 0x03"), std::string::npos) << dump();

    // Reading needs real hardware.
    EXPECT_FALSE(gateway.readControllerSettings());
    EXPECT_FALSE(gateway.readSlotDetails(0));
    EXPECT_FALSE(gateway.readLiveTravel({2, 2}));
    EXPECT_EQ(gateway.describeDevice().variant, kbd::KeyboardVariant::Iso);
}

// The firmware cuts the joystick collection from its shared HID interface in XInput mode
// (analog_matrix/usb_descriptor_override.c), so its presence tells which mode the PC enumerated.
TEST(KeyboardDeviceTest, JoystickCollectionMarksHidJoystickMode) {
    using Usages = std::vector<std::pair<std::uint16_t, std::uint16_t>>;
    const Usages xinput{{0xFF60, 0x61}, {0x01, 0x06}, {0x01, 0x02}, {0x0C, 0x01}, {0x01, 0x80}};
    Usages joystick = xinput;
    joystick.emplace_back(0x01, 0x04);
    EXPECT_FALSE(kbd::KeyboardDevice::hasJoystickCollection(xinput));
    EXPECT_TRUE(kbd::KeyboardDevice::hasJoystickCollection(joystick));
    EXPECT_TRUE(kbd::KeyboardDevice::hasJoystickCollection(Usages{{0x01, 0x05}})) << "game pad usage counts too";
    EXPECT_FALSE(kbd::KeyboardDevice::hasJoystickCollection(Usages{{0x0C, 0x04}})) << "only the Generic Desktop page";
}
