#include "ui/PngWriter.h"

#include <algorithm>
#include <array>
#include <fstream>

namespace ui {

namespace {

    constexpr std::size_t MAX_STORED_BLOCK = 65535;

    std::array<std::uint32_t, 256> makeCrcTable() {
        std::array<std::uint32_t, 256> table{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        return table;
    }

    std::uint32_t crc32(const std::vector<std::uint8_t>& bytes, const std::size_t from) {
        static const auto table = makeCrcTable();
        std::uint32_t crc = 0xFFFFFFFFu;
        for (std::size_t i = from; i < bytes.size(); ++i) crc = table[(crc ^ bytes[i]) & 0xFFu] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFFu;
    }

    void putU32(std::vector<std::uint8_t>& out, const std::uint32_t value) {
        out.push_back(static_cast<std::uint8_t>(value >> 24));
        out.push_back(static_cast<std::uint8_t>(value >> 16));
        out.push_back(static_cast<std::uint8_t>(value >> 8));
        out.push_back(static_cast<std::uint8_t>(value));
    }

    // chunk = length, type, data, CRC(type + data)
    void putChunk(std::vector<std::uint8_t>& out, const char (&type)[5], const std::vector<std::uint8_t>& data) {
        putU32(out, static_cast<std::uint32_t>(data.size()));
        const std::size_t crcStart = out.size();
        out.insert(out.end(), type, type + 4);
        out.insert(out.end(), data.begin(), data.end());
        putU32(out, crc32(out, crcStart));
    }

    std::uint32_t adler32(const std::vector<std::uint8_t>& bytes) {
        std::uint32_t a = 1, b = 0;
        for (const std::uint8_t byte : bytes) {
            a = (a + byte) % 65521u;
            b = (b + a) % 65521u;
        }
        return (b << 16) | a;
    }

} // namespace

std::vector<std::uint8_t> encodePng(const int width, const int height, const std::span<const std::uint8_t> rgb) {
    if (width <= 0 || height <= 0 ||
        rgb.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3) {
        return {};
    }

    // Raw scanlines, each prefixed with filter type 0 ("none").
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 3;
    std::vector<std::uint8_t> raw;
    raw.reserve((rowBytes + 1) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        const auto* row = rgb.data() + static_cast<std::size_t>(y) * rowBytes;
        raw.insert(raw.end(), row, row + rowBytes);
    }

    // zlib stream made of "stored" (uncompressed) deflate blocks.
    std::vector<std::uint8_t> zlib{0x78, 0x01};
    for (std::size_t offset = 0; offset < raw.size() || offset == 0;) {
        const std::size_t length = std::min(MAX_STORED_BLOCK, raw.size() - offset);
        const bool last = offset + length >= raw.size();
        zlib.push_back(last ? 1 : 0);
        zlib.push_back(static_cast<std::uint8_t>(length & 0xFF));
        zlib.push_back(static_cast<std::uint8_t>(length >> 8));
        zlib.push_back(static_cast<std::uint8_t>(~length & 0xFF));
        zlib.push_back(static_cast<std::uint8_t>((~length >> 8) & 0xFF));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + length));
        offset += length;
        if (last) break;
    }
    putU32(zlib, adler32(raw));

    std::vector<std::uint8_t> header;
    putU32(header, static_cast<std::uint32_t>(width));
    putU32(header, static_cast<std::uint32_t>(height));
    header.insert(header.end(), {8, 2, 0, 0, 0}); // 8-bit, truecolour RGB, default compression / filter, no interlace

    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    putChunk(png, "IHDR", header);
    putChunk(png, "IDAT", zlib);
    putChunk(png, "IEND", {});
    return png;
}

bool writePng(const std::filesystem::path& path, const int width, const int height, const std::span<const std::uint8_t> rgb) {
    const auto bytes = encodePng(width, height, rgb);
    if (bytes.empty()) return false;

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return file.good();
}

} // namespace ui
