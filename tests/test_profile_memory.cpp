#include <gtest/gtest.h>

#include "kbd/ProfileMemory.h"

#include <array>
#include <random>
#include <vector>

// Golden values: produced by compiling Keychron's own `analog_key_config_t` (packed bitfields, from
// qmk_firmware keyboards/keychron/common/analog_matrix/analog_matrix_type.h) with GCC and dumping its
// four bytes. Reading them back with our decoder proves the bit layout documented in ProfileMemory.h.
namespace {

    struct Golden {
        kbd::GlobalTravelConfig config;
        std::array<std::uint8_t, 4> bytes;
    };

    const std::vector<Golden>& goldenVectors() {
        static const std::vector<Golden> vectors{
            // mode, act, press, release, advMode, advData
            {{1, 20, 4, 4, 0, 0},       {0x51, 0x04, 0x01, 0x00}}, // firmware defaults: regular, 2.0 mm, 0.4 mm
            {{2, 3, 1, 1, 0, 0},        {0x0E, 0x41, 0x00, 0x00}}, // Rapid Trigger, 0.3 mm, 0.1 / 0.1 mm
            {{1, 39, 39, 39, 0, 0},     {0x9D, 0xE7, 0x09, 0x00}}, // upper firmware limits
            {{2, 1, 63, 63, 15, 255},   {0x06, 0xFF, 0xFF, 0xFF}}, // every bit of every field set
            {{0, 0, 0, 0, 0, 0},        {0x00, 0x00, 0x00, 0x00}},
        };
        return vectors;
    }

}

TEST(ProfileMemoryTest, DecodesFirmwareGoldenVectors) {
    for (const auto& golden : goldenVectors()) {
        const auto decoded = kbd::ProfileMemory::decodeGlobalTravel(golden.bytes);
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(*decoded, golden.config) << "bytes " << std::hex << int(golden.bytes[0]) << " " << int(golden.bytes[1])
                                           << " " << int(golden.bytes[2]) << " " << int(golden.bytes[3]);
    }
}

TEST(ProfileMemoryTest, EncodesFirmwareGoldenVectors) {
    for (const auto& golden : goldenVectors()) {
        EXPECT_EQ(kbd::ProfileMemory::encodeGlobalTravel(golden.config), golden.bytes);
    }
}

TEST(ProfileMemoryTest, EveryFieldSurvivesARoundTrip) {
    std::mt19937 random(12345);
    for (int i = 0; i < 5000; ++i) {
        kbd::GlobalTravelConfig config;
        config.mode              = random() % 4;
        config.actuationTenths   = random() % 64;
        config.pressSensTenths   = random() % 64;
        config.releaseSensTenths = random() % 64;
        config.advancedMode      = random() % 16;
        config.advancedData      = random() % 256;

        const auto bytes = kbd::ProfileMemory::encodeGlobalTravel(config);
        EXPECT_EQ(kbd::ProfileMemory::decodeGlobalTravel(bytes), config);
    }
}

TEST(ProfileMemoryTest, OnlyUsesTheFirstFourBytesOfALargerDump) {
    std::vector<std::uint8_t> dump(860, 0xAB);
    dump[0] = 0x0E; dump[1] = 0x41; dump[2] = 0x00; dump[3] = 0x00;

    const auto decoded = kbd::ProfileMemory::decodeGlobalTravel(dump);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->actuationTenths, 3);
    EXPECT_TRUE(decoded->isRapidTrigger());
}

TEST(ProfileMemoryTest, RejectsTooShortInput) {
    const std::array<std::uint8_t, 3> tooShort{0x51, 0x04, 0x01};
    EXPECT_FALSE(kbd::ProfileMemory::decodeGlobalTravel(tooShort).has_value());
    EXPECT_FALSE(kbd::ProfileMemory::decodeGlobalTravel({}).has_value());
}

TEST(ProfileMemoryTest, EncodingMasksValuesToTheirFieldWidths) {
    kbd::GlobalTravelConfig config;
    config.mode = 0xFF;            // 2 bits
    config.actuationTenths = 0xFF; // 6 bits
    config.pressSensTenths = 0xFF;
    config.releaseSensTenths = 0xFF;
    config.advancedMode = 0xFF;    // 4 bits

    const auto decoded = kbd::ProfileMemory::decodeGlobalTravel(kbd::ProfileMemory::encodeGlobalTravel(config));

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->mode, 3);
    EXPECT_EQ(decoded->actuationTenths, 63);
    EXPECT_EQ(decoded->pressSensTenths, 63);
    EXPECT_EQ(decoded->releaseSensTenths, 63);
    EXPECT_EQ(decoded->advancedMode, 15);
}

TEST(ProfileMemoryTest, OnlyModeTwoIsRapidTrigger) {
    // Firmware enum: AKM_GLOBAL = 0, AKM_REGULAR = 1, AKM_RAPID = 2.
    EXPECT_FALSE((kbd::GlobalTravelConfig{0}).isRapidTrigger());
    EXPECT_FALSE((kbd::GlobalTravelConfig{1}).isRapidTrigger());
    EXPECT_TRUE((kbd::GlobalTravelConfig{2}).isRapidTrigger());
}

// ---------------------------------------------------------------------------
// Whole profile: per-key entries and the firmware's "0 = use the global value" rule
// ---------------------------------------------------------------------------

TEST(ProfileMemoryTest, LayoutMatchesTheFirmwareStruct) {
    EXPECT_EQ(kbd::ProfileMemory::KEY_CONFIG_OFFSET, 4u);
    EXPECT_EQ(kbd::ProfileMemory::OKMC_OFFSET, 388u);
    EXPECT_EQ(kbd::ProfileMemory::SOCD_OFFSET, 768u);
    EXPECT_EQ(kbd::ProfileMemory::NAME_OFFSET, 828u);
    EXPECT_EQ(kbd::ProfileMemory::CRC_OFFSET, 858u);
    EXPECT_EQ(kbd::ProfileMemory::PROFILE_SIZE, 860u);
    EXPECT_EQ(kbd::ProfileMemory::keyConfigOffset(0, 0), 4u);
    EXPECT_EQ(kbd::ProfileMemory::keyConfigOffset(5, 15), 4u + 95u * 4u);
}

TEST(ProfileMemoryTest, KeyEntriesRoundTripAtTheirOwnOffset) {
    std::vector<std::uint8_t> profile(kbd::ProfileMemory::PROFILE_SIZE, 0);
    const kbd::KeyTravelConfig w{2, 3, 1, 2, 0, 0};

    ASSERT_TRUE(kbd::ProfileMemory::encodeKeyTravel(profile, 2, 2, w));
    EXPECT_EQ(kbd::ProfileMemory::decodeKeyTravel(profile, 2, 2), w);

    // Only the four bytes of that key changed.
    for (std::size_t i = 0; i < profile.size(); ++i) {
        const bool inKey = i >= 140 && i < 144;
        if (!inKey) {
            EXPECT_EQ(profile[i], 0) << i;
        }
    }
}

TEST(ProfileMemoryTest, KeyAccessOutsideTheMatrixOrDumpFails) {
    std::vector<std::uint8_t> profile(kbd::ProfileMemory::PROFILE_SIZE, 0);
    EXPECT_FALSE(kbd::ProfileMemory::decodeKeyTravel(profile, 6, 0).has_value());
    EXPECT_FALSE(kbd::ProfileMemory::decodeKeyTravel(profile, 0, 16).has_value());
    EXPECT_FALSE(kbd::ProfileMemory::encodeKeyTravel(profile, 6, 0, {}));

    std::vector<std::uint8_t> shortDump(100, 0);
    EXPECT_FALSE(kbd::ProfileMemory::decodeKeyTravel(shortDump, 5, 15).has_value());
}

TEST(ProfileMemoryTest, ZeroKeyFieldsTakeTheGlobalValue) {
    const kbd::GlobalTravelConfig global{2, 10, 3, 0, 0, 0}; // release 0 = same as press

    const auto followAll = kbd::ProfileMemory::resolveKeyTravel(global, kbd::KeyTravelConfig{0, 0, 0, 0, 0, 0});
    EXPECT_EQ(followAll.mode, 2);
    EXPECT_EQ(followAll.actuationTenths, 10);
    EXPECT_EQ(followAll.pressSensTenths, 3);
    EXPECT_EQ(followAll.releaseSensTenths, 3);

    const auto ownActuationOnly = kbd::ProfileMemory::resolveKeyTravel(global, kbd::KeyTravelConfig{0, 2, 0, 0, 0, 0});
    EXPECT_EQ(ownActuationOnly.mode, 2);
    EXPECT_EQ(ownActuationOnly.actuationTenths, 2);

    const auto own = kbd::ProfileMemory::resolveKeyTravel(global, kbd::KeyTravelConfig{1, 25, 4, 5, 0, 0});
    EXPECT_EQ(own, (kbd::KeyTravelConfig{1, 25, 4, 5, 0, 0}));
}

TEST(ProfileMemoryTest, SocdPairsPackRowAndColumnIntoOneByte) {
    std::vector<std::uint8_t> profile(kbd::ProfileMemory::PROFILE_SIZE, 0);
    const kbd::SocdPair pair{5, 6, 3, 2, 6}; // Space / S, neutral

    ASSERT_TRUE(kbd::ProfileMemory::encodeSocd(profile, 2, pair));
    const std::size_t at = kbd::ProfileMemory::SOCD_OFFSET + 2 * 3;
    EXPECT_EQ(profile[at], 5 | (6 << 3));     // row in bits 0-2, column in bits 3-7 (socd_config_t)
    EXPECT_EQ(profile[at + 1], 3 | (2 << 3));
    EXPECT_EQ(profile[at + 2], 6);
    EXPECT_EQ(kbd::ProfileMemory::decodeSocd(profile, 2), pair);

    EXPECT_FALSE(kbd::ProfileMemory::decodeSocd(profile, 20).has_value());
    EXPECT_FALSE(kbd::ProfileMemory::encodeSocd(profile, 20, pair));
}
