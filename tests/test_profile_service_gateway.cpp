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
    // 0xA9 0x14 <slot> <mode> <act> <press> <release> <entire keyboard>
    EXPECT_NE(dump().find("0xa9 0x14 0x01 0x01 0x03 0x01 0x01 0x01"), std::string::npos) << dump();
}

TEST_F(GatewayTest, ApplyActuationClampsOutOfRangeValues) {
    const auto result = gateway.applyActuation(2, ui::ActuationSettings{false, 0, 200, 0});

    EXPECT_TRUE(result.ok) << result.message;
    // act 0 -> 1, press 200 -> 39 (0x27), release 0 -> 1
    EXPECT_NE(dump().find("0xa9 0x14 0x02 0x00 0x01 0x27 0x01 0x01"), std::string::npos) << dump();
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

TEST_F(GatewayTest, FlashStreamsTheWholeProfileAndCommitsIt) {
    const auto result = gateway.flashSlotProfile(0);

    EXPECT_TRUE(result.ok) << result.message;
    EXPECT_EQ(countOccurrences(dump(), "0xa9 0x12 0x00"), 34) << "860 bytes = 33 chunks of 26 bytes + 1 of 2";
    EXPECT_EQ(countOccurrences(dump(), "0xa9 0x1f 0x00"), 1) << "followed by the flash commit";
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
