#include <gtest/gtest.h>

#include "support/TempDir.h"
#include "ui/AppModel.h"
#include "ui/DemoProfileGateway.h"
#include "ui/PngWriter.h"
#include "ui/ProfileController.h"

#include <chrono>
#include <fstream>
#include <thread>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// PNG writer: decode our own output (CRCs, zlib stored blocks, Adler-32) and compare the pixels
// ---------------------------------------------------------------------------

namespace {

    std::uint32_t readU32(const std::vector<std::uint8_t>& b, const std::size_t at) {
        return (static_cast<std::uint32_t>(b[at]) << 24) | (static_cast<std::uint32_t>(b[at + 1]) << 16) |
               (static_cast<std::uint32_t>(b[at + 2]) << 8) | b[at + 3];
    }

    std::uint32_t crc32(const std::uint8_t* data, const std::size_t size) {
        std::uint32_t crc = 0xFFFFFFFFu;
        for (std::size_t i = 0; i < size; ++i) {
            crc ^= data[i];
            for (int k = 0; k < 8; ++k) crc = (crc & 1u) ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
        }
        return crc ^ 0xFFFFFFFFu;
    }

    struct Decoded {
        int                       width = 0, height = 0;
        std::vector<std::uint8_t> rgb;
        bool                      valid = false;
    };

    Decoded decodeStoredPng(const std::vector<std::uint8_t>& png) {
        Decoded out;
        const std::vector<std::uint8_t> signature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
        if (png.size() < 8 || !std::equal(signature.begin(), signature.end(), png.begin())) return out;

        std::vector<std::uint8_t> zlib;
        std::size_t pos = 8;
        bool sawEnd = false;
        while (pos + 12 <= png.size()) {
            const std::uint32_t length = readU32(png, pos);
            const std::string type(reinterpret_cast<const char*>(&png[pos + 4]), 4);
            if (pos + 12 + length > png.size()) return out;
            if (readU32(png, pos + 8 + length) != crc32(&png[pos + 4], 4 + length)) return out; // bad CRC

            if (type == "IHDR") {
                out.width  = static_cast<int>(readU32(png, pos + 8));
                out.height = static_cast<int>(readU32(png, pos + 12));
                if (png[pos + 16] != 8 || png[pos + 17] != 2) return out; // 8-bit RGB expected
            } else if (type == "IDAT") {
                zlib.insert(zlib.end(), png.begin() + static_cast<std::ptrdiff_t>(pos + 8),
                            png.begin() + static_cast<std::ptrdiff_t>(pos + 8 + length));
            } else if (type == "IEND") {
                sawEnd = true;
            }
            pos += 12 + length;
        }
        if (!sawEnd || zlib.size() < 6) return out;

        std::vector<std::uint8_t> raw;
        std::size_t z = 2; // skip the zlib header
        for (bool last = false; !last;) {
            last = (zlib[z] & 1) != 0;
            if ((zlib[z] >> 1) != 0) return out; // only stored blocks are produced
            const std::size_t len = zlib[z + 1] | (zlib[z + 2] << 8);
            const std::size_t nlen = zlib[z + 3] | (zlib[z + 4] << 8);
            if ((len ^ 0xFFFFu) != nlen) return out;
            raw.insert(raw.end(), zlib.begin() + static_cast<std::ptrdiff_t>(z + 5), zlib.begin() + static_cast<std::ptrdiff_t>(z + 5 + len));
            z += 5 + len;
        }

        std::uint32_t a = 1, b = 0;
        for (const auto byte : raw) { a = (a + byte) % 65521u; b = (b + a) % 65521u; }
        if (readU32(zlib, z) != ((b << 16) | a)) return out; // Adler-32 mismatch

        const std::size_t rowBytes = static_cast<std::size_t>(out.width) * 3;
        for (int y = 0; y < out.height; ++y) {
            const std::size_t row = static_cast<std::size_t>(y) * (rowBytes + 1);
            if (raw[row] != 0) return out; // filter type none
            out.rgb.insert(out.rgb.end(), raw.begin() + static_cast<std::ptrdiff_t>(row + 1), raw.begin() + static_cast<std::ptrdiff_t>(row + 1 + rowBytes));
        }
        out.valid = true;
        return out;
    }

    std::vector<std::uint8_t> gradient(const int width, const int height) {
        std::vector<std::uint8_t> rgb;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                rgb.push_back(static_cast<std::uint8_t>(x * 7));
                rgb.push_back(static_cast<std::uint8_t>(y * 11));
                rgb.push_back(static_cast<std::uint8_t>(x ^ y));
            }
        }
        return rgb;
    }

}

TEST(PngWriterTest, SmallImageRoundTrips) {
    const auto pixels = gradient(7, 5);

    const auto decoded = decodeStoredPng(ui::encodePng(7, 5, pixels));

    ASSERT_TRUE(decoded.valid);
    EXPECT_EQ(decoded.width, 7);
    EXPECT_EQ(decoded.height, 5);
    EXPECT_EQ(decoded.rgb, pixels);
}

TEST(PngWriterTest, ImagesLargerThanOneStoredBlockRoundTrip) {
    const auto pixels = gradient(200, 150); // 90 150 raw bytes > 65 535: needs two deflate blocks

    const auto decoded = decodeStoredPng(ui::encodePng(200, 150, pixels));

    ASSERT_TRUE(decoded.valid);
    EXPECT_EQ(decoded.rgb, pixels);
}

TEST(PngWriterTest, RejectsInconsistentInput) {
    EXPECT_TRUE(ui::encodePng(4, 4, gradient(4, 3)).empty()) << "buffer too small for 4x4";
    EXPECT_TRUE(ui::encodePng(0, 4, {}).empty());
    EXPECT_TRUE(ui::encodePng(4, -1, {}).empty());
}

TEST(PngWriterTest, WritesAFileToDisk) {
    test::TempDir dir;
    const auto path = dir / "shot.png";

    ASSERT_TRUE(ui::writePng(path, 3, 2, gradient(3, 2)));

    std::ifstream file(path, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_TRUE(decodeStoredPng(bytes).valid);
    EXPECT_FALSE(ui::writePng(dir / "missing-dir/shot.png", 3, 2, gradient(3, 2)));
}

// ---------------------------------------------------------------------------
// Demo gateway (simulated keyboard), alone and driven by the real controller
// ---------------------------------------------------------------------------

TEST(DemoGatewayTest, DescribesAConnectedKeyboardWithThreeProfiles) {
    ui::DemoProfileGateway gateway(0ms);

    EXPECT_TRUE(gateway.describeDevice().connected);
    for (const auto& slot : gateway.describeSlots()) {
        EXPECT_TRUE(slot.hasProfile);
        EXPECT_FALSE(slot.name.empty());
        EXPECT_TRUE(slot.storedSettings.has_value());
    }
    EXPECT_EQ(gateway.queryActiveSlot(), 0);
}

TEST(DemoGatewayTest, CyclingWrapsInBothDirections) {
    ui::DemoProfileGateway gateway(0ms);

    gateway.cycleProfile(false);
    EXPECT_EQ(gateway.queryActiveSlot(), 2);
    gateway.cycleProfile(true);
    gateway.cycleProfile(true);
    EXPECT_EQ(gateway.queryActiveSlot(), 1);
}

TEST(DemoGatewayTest, AppliedSettingsCanBeReadBackAndFlashRestoresTheProfile) {
    ui::DemoProfileGateway gateway(0ms);

    ASSERT_TRUE(gateway.applyActuation(1, ui::ActuationSettings{false, 17, 2, 2}).ok);
    EXPECT_EQ(gateway.readActuation(1, std::nullopt), (ui::ActuationSettings{false, 17, 2, 2}));

    ASSERT_TRUE(gateway.flashSlotProfile(1).ok);
    EXPECT_EQ(gateway.readActuation(1, std::nullopt), (ui::ActuationSettings{true, 3, 1, 1})) << "back to the profile file's values";
}

TEST(DemoGatewayTest, TheRealControllerDrivesItEndToEnd) {
    ui::DemoProfileGateway gateway(0ms);
    ui::AppModel model;
    ui::ProfileController controller(gateway, model, 20ms);
    controller.start();

    const auto waitFor = [&](auto&& predicate) {
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (!predicate() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
        return predicate();
    };
    const auto idleWith = [&](const ui::StatusSeverity severity) {
        return [&model, severity] {
            const auto s = model.snapshot();
            return !s.busy && s.status.severity == severity;
        };
    };

    ASSERT_TRUE(waitFor([&] { return model.snapshot().slots[1].name == "Tekken 8 Mishima"; }));
    EXPECT_EQ(model.snapshot().activeSlot, 0);

    controller.activateSlot(2);
    ASSERT_TRUE(waitFor(idleWith(ui::StatusSeverity::Success)));
    EXPECT_EQ(model.snapshot().activeSlot, 2);

    controller.applyActuation(1, ui::ActuationSettings{true, 5, 2, 2}, true);
    ASSERT_TRUE(waitFor([&] {
        const auto s = model.snapshot();
        return !s.busy && s.status.text.find("Saved") != std::string::npos;
    }));
    EXPECT_EQ(model.snapshot().status.severity, ui::StatusSeverity::Success);
    EXPECT_NE(model.snapshot().status.text.find("verified"), std::string::npos);
    EXPECT_EQ(model.snapshot().actuation[1], (ui::ActuationSettings{true, 5, 2, 2}));

    controller.cycleProfile(true);
    ASSERT_TRUE(waitFor([&] { return !model.snapshot().busy && model.snapshot().activeSlot == 0; }));

    controller.stop();
}
