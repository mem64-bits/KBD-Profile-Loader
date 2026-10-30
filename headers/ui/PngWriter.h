#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace ui {

    // Minimal PNG encoder (8-bit RGB, no external libraries; the image data is stored uncompressed, so
    // files are large but always valid). Used by the --screenshot option. `rgb` holds width * height * 3
    // bytes, rows top to bottom.
    [[nodiscard]] std::vector<std::uint8_t> encodePng(int width, int height, std::span<const std::uint8_t> rgb);

    bool writePng(const std::filesystem::path& path, int width, int height, std::span<const std::uint8_t> rgb);

} // namespace ui
