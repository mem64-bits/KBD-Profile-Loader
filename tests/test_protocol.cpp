#include <gtest/gtest.h>
#include "kbd/KeychronProtocol.h"
#include "kbd/ProfileMemory.h"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <array>
#include <vector>

TEST(KeychronProtocolTest, SlotSelectPacketStructure) {
    auto packet = kbd::KeychronProtocol::createSlotSelectPacket(1); // Slot 2

    ASSERT_EQ(packet.size(), 32);
    EXPECT_EQ(packet[0], 0xA9); // Keychron AMC Opcode
    EXPECT_EQ(packet[1], 0x11); // AMC_SELECT_PROFILE
    EXPECT_EQ(packet[2], 0x01); // Slot Index (0-indexed: Slot 2)
}

TEST(KeychronProtocolTest, StatusQueryPacketStructure) {
    auto packet = kbd::KeychronProtocol::createStatusQueryPacket();

    ASSERT_EQ(packet.size(), 32);
    EXPECT_EQ(packet[0], 0xA9);
    EXPECT_EQ(packet[1], 0x10); // AMC_GET_PROFILES_INFO
}

TEST(KeychronProtocolTest, SaveProfilePacketStructure) {
    auto packet = kbd::KeychronProtocol::createSaveProfilePacket(2); // Slot 3

    ASSERT_EQ(packet.size(), 32);
    EXPECT_EQ(packet[0], 0xA9);
    EXPECT_EQ(packet[1], 0x1F); // AMC_SAVE_PROFILE (Flash commit)
    EXPECT_EQ(packet[2], 0x02); // Slot Index
}

TEST(KeychronProtocolTest, FastTravelPacketStructure) {
    // Slot 1 (Tekken 8): 0.3mm Actuation, RT ON, 0.1mm Press, 0.1mm Release, Entire Keyboard
    auto packet = kbd::KeychronProtocol::createFastTravelPacket(1, true, 3, 1, 1, true);

    ASSERT_EQ(packet.size(), 32);
    EXPECT_EQ(packet[0], 0xA9);
    EXPECT_EQ(packet[1], 0x14); // AMC_SET_TRAVEL
    EXPECT_EQ(packet[2], 0x01); // Slot 2
    EXPECT_EQ(packet[3], 0x02); // Mode: firmware AKM_RAPID = 2 (1 would be AKM_REGULAR)
    EXPECT_EQ(packet[4], 3);    // 0.3mm Actuation (3 tenths)
    EXPECT_EQ(packet[5], 1);    // 0.10mm Press Sensitivity
    EXPECT_EQ(packet[6], 1);    // 0.10mm Release Sensitivity
    EXPECT_EQ(packet[7], 0x01); // 1 = Entire Keyboard
}

TEST(KeychronProtocolTest, SetProfileNamePacketStructure) {
    std::string name = "Tekken 8 Mishima";
    auto packet = kbd::KeychronProtocol::createSetProfileNamePacket(1, name);

    ASSERT_EQ(packet.size(), 32);
    EXPECT_EQ(packet[0], 0xA9);
    EXPECT_EQ(packet[1], 0x13); // AMC_SET_PROFILE_NAME
    EXPECT_EQ(packet[2], 0x01); // Slot Index

    // profile_set_name(data[2], data[3], &data[4]): length at byte 3, text from byte 4
    EXPECT_EQ(packet[3], name.length());
    std::string extracted(reinterpret_cast<char*>(packet.data() + 4), name.length());
    EXPECT_EQ(extracted, name);
}

TEST(KeychronProtocolTest, ProfileNamesAreCutToTheFirmwareLimit) {
    const auto packet = kbd::KeychronProtocol::createSetProfileNamePacket(0, std::string(40, 'x'));
    ASSERT_EQ(packet.size(), 32u);
    EXPECT_EQ(packet[3], 28);
    EXPECT_EQ(packet[31], 'x'); // 4 header bytes + 28 characters fill the report exactly
}

TEST(KeychronProtocolTest, RegularModeIsOneForTheWholeKeyboard) {
    const auto packet = kbd::KeychronProtocol::createFastTravelPacket(2, false, 20, 4, 4, true);
    EXPECT_EQ(packet[3], 0x01); // AKM_REGULAR; 0 (AKM_GLOBAL) is rejected for the whole keyboard
}

TEST(KeychronProtocolTest, KeyTravelPacketCarriesLittleEndianRowMasks) {
    // W (row 2, col 2), A S D (row 3, cols 1-3) and Right arrow (row 5, col 14).
    std::array<std::uint32_t, kbd::KeychronProtocol::MATRIX_ROWS> masks{};
    masks[2] = 1u << 2;
    masks[3] = (1u << 1) | (1u << 2) | (1u << 3);
    masks[5] = 1u << 14;

    const auto packet = kbd::KeychronProtocol::createKeyTravelPacket(1, 2, 3, 1, 2, masks);

    ASSERT_EQ(packet.size(), 32);
    EXPECT_EQ(packet[0], 0xA9);
    EXPECT_EQ(packet[1], 0x14);
    EXPECT_EQ(packet[2], 0x01); // slot
    EXPECT_EQ(packet[3], 0x02); // Rapid Trigger
    EXPECT_EQ(packet[4], 3);
    EXPECT_EQ(packet[5], 1);
    EXPECT_EQ(packet[6], 2);
    EXPECT_EQ(packet[7], 0x00); // per key
    // rows 0, 1: no keys
    for (int i = 8; i < 14; ++i) EXPECT_EQ(packet[i], 0x00) << i;
    // row 2 at [14..16], row 3 at [17..19], row 5 at [23..25]
    EXPECT_EQ(packet[14], 0x04);
    EXPECT_EQ(packet[17], 0x0E);
    EXPECT_EQ(packet[23], 0x00);
    EXPECT_EQ(packet[24], 0x40); // bit 14 = bit 6 of the second byte
    EXPECT_EQ(packet[25], 0x00);
    for (int i = 26; i < 32; ++i) EXPECT_EQ(packet[i], 0x00) << i;
}

TEST(KeychronProtocolTest, ProfileReadPacketAsksForAnOffsetAndSize) {
    const auto packet = kbd::KeychronProtocol::createProfileReadPacket(2, 0x0164, 26);
    EXPECT_EQ(packet[0], 0xA9);
    EXPECT_EQ(packet[1], 0x12);
    EXPECT_EQ(packet[2], 0x02);
    EXPECT_EQ(packet[3], 0x64); // offset, little-endian
    EXPECT_EQ(packet[4], 0x01);
    EXPECT_EQ(packet[5], 26);
    EXPECT_THROW((void)kbd::KeychronProtocol::createProfileReadPacket(0, 0, 27), std::invalid_argument);
}

TEST(KeychronProtocolTest, RealtimeTravelPacketAsksForOneKey) {
    auto packet = kbd::KeychronProtocol::createRealtimeTravelPacket(3, 2); // S

    ASSERT_EQ(packet.size(), 32);
    EXPECT_EQ(packet[0], 0xA9);
    EXPECT_EQ(packet[1], 0x30); // AMC_GET_REALTIME_TRAVEL
    EXPECT_EQ(packet[2], 3);
    EXPECT_EQ(packet[3], 2);
}

TEST(KeychronProtocolTest, Full860ByteStreamChunkingMath) {
    std::array<uint8_t, 860> dummyMemory{};
    for (size_t i = 0; i < dummyMemory.size(); ++i) {
        dummyMemory[i] = static_cast<uint8_t>(i & 0xFF);
    }

    auto chunks = kbd::KeychronProtocol::generateMemoryStreamPackets(1, dummyMemory);

    ASSERT_EQ(chunks.size(), 34)
        << "860 bytes must be split into 33 chunks of 26 bytes and 1 chunk of 2 bytes";

    // Validate Chunk 0 (Offset 0)
    EXPECT_EQ(chunks[0][0], 0xA9);
    EXPECT_EQ(chunks[0][1], 0x12);
    EXPECT_EQ(chunks[0][2], 0x01);
    EXPECT_EQ(chunks[0][3], 0x00); // Offset Lo
    EXPECT_EQ(chunks[0][4], 0x00); // Offset Hi
    EXPECT_EQ(chunks[0][5], 26);   // Chunk Size

    // Validate Chunk 32 (Offset 832 = 0x0340)
    EXPECT_EQ(chunks[32][3], 0x40); // 832 & 0xFF
    EXPECT_EQ(chunks[32][4], 0x03); // (832 >> 8) & 0xFF
    EXPECT_EQ(chunks[32][5], 26);

    // Validate Chunk 33 (Offset 858 = 0x035A, Final 2 bytes)
    EXPECT_EQ(chunks[33][3], 0x5A); // 858 & 0xFF
    EXPECT_EQ(chunks[33][4], 0x03); // (858 >> 8) & 0xFF
    EXPECT_EQ(chunks[33][5], 2);    // Strictly 2 bytes left (860 - 858)
    EXPECT_EQ(chunks[33][6], (858 & 0xFF));
    EXPECT_EQ(chunks[33][7], (859 & 0xFF));
}

TEST(KeychronProtocolTest, ChunkPayloadOverflowThrowsException) {
    std::vector<uint8_t> oversizedPayload(27, 0xFF); // 27 bytes > 26 byte limit

    EXPECT_THROW(
        kbd::KeychronProtocol::createMemoryChunkPacket(0, 0, oversizedPayload),
        std::invalid_argument
    );
}
// ---------------------------------------------------------------------------
// Profile files in the Keychron Launcher format
// ---------------------------------------------------------------------------

namespace {

    nlohmann::json launcherKey(int mode, int distance, int press, int release) {
        return {{"mode", mode}, {"distance", distance}, {"advance_press", press}, {"advance_release", release},
                {"dksIndex", nullptr}, {"advanceMode", 0}};
    }

} // namespace

TEST(KeychronProtocolTest, LauncherProfilePutsEveryKeyWhereTheFirmwareKeepsIt) {
    nlohmann::json profile;
    profile["name"]   = "Tekken 8";
    profile["global"] = launcherKey(2, 3, 1, 1);
    profile["keyMatrix"] = nlohmann::json::array();
    for (int r = 0; r < 6; ++r) {
        nlohmann::json row = nlohmann::json::array();
        for (int c = 0; c < 16; ++c) {
            auto key = launcherKey(0, 0, 0, 0);
            key["row"] = r;
            key["col"] = c;
            row.push_back(key);
        }
        profile["keyMatrix"].push_back(row);
    }
    profile["keyMatrix"][2][2] = launcherKey(2, 2, 1, 1); // W, no row / col: position comes from the array
    profile["keyMatrix"][5][15] = launcherKey(1, 39, 0, 0);

    const auto memory = kbd::KeychronProtocol::extractMemoryBuffer(profile);

    const auto global = kbd::ProfileMemory::decodeGlobalTravel(memory);
    ASSERT_TRUE(global);
    EXPECT_EQ(*global, (kbd::GlobalTravelConfig{2, 3, 1, 1, 0, 0}));

    // key_config[2][2] lives at 4 + (2 * 16 + 2) * 4 = 140
    EXPECT_EQ(kbd::ProfileMemory::keyConfigOffset(2, 2), 140u);
    EXPECT_EQ(kbd::ProfileMemory::decodeKeyTravel(memory, 2, 2), (kbd::KeyTravelConfig{2, 2, 1, 1, 0, 0}));
    EXPECT_EQ(kbd::ProfileMemory::decodeKeyTravel(memory, 5, 15), (kbd::KeyTravelConfig{1, 39, 0, 0, 0, 0}));
    EXPECT_EQ(kbd::ProfileMemory::decodeKeyTravel(memory, 0, 0), (kbd::KeyTravelConfig{0, 0, 0, 0, 0, 0}));

    // The name is at offset 828, not over the first keys.
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(memory.data() + kbd::ProfileMemory::NAME_OFFSET), 8), "Tekken 8");
    for (std::size_t i = kbd::ProfileMemory::OKMC_OFFSET; i < kbd::ProfileMemory::NAME_OFFSET; ++i) EXPECT_EQ(memory[i], 0) << i;
}

TEST(KeychronProtocolTest, LauncherKeysOutsideTheMatrixAreIgnored) {
    nlohmann::json profile;
    profile["global"] = launcherKey(1, 20, 4, 4);
    auto stray = launcherKey(1, 10, 0, 0);
    stray["row"] = 9;
    stray["col"] = 40;
    profile["keyMatrix"] = nlohmann::json::array({nlohmann::json::array({stray})});

    const auto memory = kbd::KeychronProtocol::extractMemoryBuffer(profile);
    for (std::size_t i = kbd::ProfileMemory::KEY_CONFIG_OFFSET; i < kbd::ProfileMemory::OKMC_OFFSET; ++i) EXPECT_EQ(memory[i], 0) << i;
}

TEST(KeychronProtocolTest, ProfileWithoutSettingsIsRejected) {
    EXPECT_THROW((void)kbd::KeychronProtocol::extractMemoryBuffer(nlohmann::json{{"name", "x"}}), std::runtime_error);
}

TEST(KeychronProtocolTest, LauncherEditCopyWinsUnlessTheKeyHasUnappliedEdits) {
    nlohmann::json clean = launcherKey(0, 20, 4, 4);
    clean["dirty"] = false;
    clean["edit"]  = {{"mode", 0}, {"distance", 0}, {"advance_press", 0}, {"advance_release", 0}};
    nlohmann::json dirty = launcherKey(1, 12, 3, 3);
    dirty["dirty"] = true;
    dirty["edit"]  = {{"mode", 2}, {"distance", 5}, {"advance_press", 1}, {"advance_release", 1}};

    nlohmann::json profile;
    profile["global"]    = launcherKey(1, 20, 4, 4);
    profile["keyMatrix"] = nlohmann::json::array({nlohmann::json::array({clean, dirty})});

    const auto memory = kbd::KeychronProtocol::extractMemoryBuffer(profile);
    EXPECT_EQ(kbd::ProfileMemory::decodeKeyTravel(memory, 0, 0), (kbd::KeyTravelConfig{0, 0, 0, 0, 0, 0})) << "raw copy";
    EXPECT_EQ(kbd::ProfileMemory::decodeKeyTravel(memory, 0, 1), (kbd::KeyTravelConfig{1, 12, 3, 3, 0, 0})) << "applied values";
}

TEST(KeychronProtocolTest, SnapTapEntriesOutsideTheLimitsAreSkipped) {
    const auto keyAt = [](int row, int col) { return nlohmann::json{{"row", row}, {"col", col}}; };
    nlohmann::json profile;
    profile["global"]  = launcherKey(1, 20, 4, 4);
    profile["snapTap"] = nlohmann::json::array({
        {{"keys", {keyAt(3, 1), keyAt(3, 3)}}, {"type", 6}, {"index", 0}},  // ok
        {{"keys", {keyAt(3, 1), keyAt(3, 3)}}, {"type", 7}, {"index", 1}},  // type out of range
        {{"keys", {keyAt(3, 1), keyAt(3, 3)}}, {"type", 2}, {"index", 20}}, // index out of range
        {{"keys", {keyAt(9, 1), keyAt(3, 3)}}, {"type", 2}, {"index", 2}},  // row out of range
    });

    const auto memory = kbd::KeychronProtocol::extractMemoryBuffer(profile);
    EXPECT_EQ(kbd::ProfileMemory::decodeSocd(memory, 0), (kbd::SocdPair{3, 1, 3, 3, 6}));
    EXPECT_FALSE(kbd::ProfileMemory::decodeSocd(memory, 1)->isSet());
    EXPECT_FALSE(kbd::ProfileMemory::decodeSocd(memory, 2)->isSet());
}

TEST(KeychronProtocolTest, AdvanceModeAndSocdPackets) {
    const auto adv = kbd::KeychronProtocol::createAdvanceModePacket(2, kbd::KeychronProtocol::ADV_MODE_GAME_CONTROLLER, 3, 1, 7);
    EXPECT_EQ(std::vector<std::uint8_t>(adv.begin(), adv.begin() + 8), (std::vector<std::uint8_t>{0xA9, 0x15, 2, 2, 3, 1, 7, 0}));

    const auto socd = kbd::KeychronProtocol::createSocdPacket(1, 4, 3, 1, 3, 3, 6);
    EXPECT_EQ(std::vector<std::uint8_t>(socd.begin(), socd.begin() + 10), (std::vector<std::uint8_t>{0xA9, 0x16, 1, 3, 1, 3, 3, 4, 6, 0}));
}
