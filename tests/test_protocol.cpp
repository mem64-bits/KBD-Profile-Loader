#include <gtest/gtest.h>
#include "kbd/KeychronProtocol.h"
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
    EXPECT_EQ(packet[3], 0x01); // Mode: 1 = Rapid Trigger ON
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

    // String should be placed starting at Byte 3
    std::string extracted(reinterpret_cast<char*>(packet.data() + 3), name.length());
    EXPECT_EQ(extracted, name);
}

TEST(KeychronProtocolTest, RealtimeTravelPollPacketStructure) {
    auto packet = kbd::KeychronProtocol::createRealtimeTravelPollPacket();

    ASSERT_EQ(packet.size(), 32);
    EXPECT_EQ(packet[0], 0xA9);
    EXPECT_EQ(packet[1], 0x30); // AMC_GET_REALTIME_TRAVEL
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