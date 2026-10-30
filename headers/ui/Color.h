#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ui {

    // 8-bit RGBA colour, independent of any UI toolkit (ImGui, Win32 and the D-Bus tray each
    // convert it to their own representation). Plain value type: rule of zero.
    struct Color {
        std::uint8_t r{0};
        std::uint8_t g{0};
        std::uint8_t b{0};
        std::uint8_t a{255};

        constexpr Color() noexcept = default;
        constexpr Color(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha = 255) noexcept
            : r(red), g(green), b(blue), a(alpha) {}

        // 0xRRGGBB -> Color
        [[nodiscard]] static constexpr Color fromRgb(std::uint32_t rgb, std::uint8_t alpha = 255) noexcept {
            return Color(static_cast<std::uint8_t>((rgb >> 16) & 0xFF),
                         static_cast<std::uint8_t>((rgb >> 8) & 0xFF),
                         static_cast<std::uint8_t>(rgb & 0xFF),
                         alpha);
        }

        // Accepts "#RGB", "#RGBA", "#RRGGBB" and "#RRGGBBAA" (leading '#' optional, any case).
        [[nodiscard]] static std::optional<Color> fromHex(std::string_view text);

        // "#RRGGBB", or "#RRGGBBAA" when includeAlpha is set.
        [[nodiscard]] std::string toHex(bool includeAlpha = false) const;

        [[nodiscard]] constexpr Color withAlpha(std::uint8_t alpha) const noexcept {
            return Color(r, g, b, alpha);
        }

        // 0xAARRGGBB, the layout used by StatusNotifierItem pixmaps.
        [[nodiscard]] constexpr std::uint32_t toArgb() const noexcept {
            return (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(r) << 16) |
                   (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b);
        }

        // Linear blend towards `other` (t is clamped to 0..1, alpha is blended too).
        [[nodiscard]] Color mix(const Color& other, float t) const noexcept;
        [[nodiscard]] Color lighten(float amount) const noexcept; // towards white, alpha preserved
        [[nodiscard]] Color darken(float amount) const noexcept;  // towards black, alpha preserved

        // WCAG relative luminance (0 = black, 1 = white); alpha is ignored.
        [[nodiscard]] float luminance() const noexcept;

        // HSV hue in degrees, 0..360 (0 for greys).
        [[nodiscard]] float hue() const noexcept;

        // WCAG contrast ratio between two colours, 1..21.
        [[nodiscard]] static float contrastRatio(const Color& first, const Color& second) noexcept;

        constexpr bool operator==(const Color&) const noexcept = default;
    };

} // namespace ui
