#include "ui/Color.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace ui {

namespace {

    int hexValue(const char c) noexcept {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    std::uint8_t toByte(const float value) noexcept {
        return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 255.0f)));
    }

    // sRGB channel -> linear light, as defined by WCAG 2.x
    float toLinear(const std::uint8_t channel) noexcept {
        const float srgb = static_cast<float>(channel) / 255.0f;
        return srgb <= 0.04045f ? srgb / 12.92f : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
    }

} // namespace

std::optional<Color> Color::fromHex(std::string_view text) {
    if (!text.empty() && text.front() == '#') text.remove_prefix(1);

    const std::size_t length = text.size();
    if (length != 3 && length != 4 && length != 6 && length != 8) return std::nullopt;

    std::array<int, 8> digits{};
    for (std::size_t i = 0; i < length; ++i) {
        digits[i] = hexValue(text[i]);
        if (digits[i] < 0) return std::nullopt;
    }

    const bool shorthand = length <= 4;
    const auto channel = [&](const std::size_t index) {
        const int value = shorthand ? digits[index] * 17 : digits[index * 2] * 16 + digits[index * 2 + 1];
        return static_cast<std::uint8_t>(value);
    };

    const bool hasAlpha = (length == 4 || length == 8);
    return Color(channel(0), channel(1), channel(2), hasAlpha ? channel(3) : static_cast<std::uint8_t>(255));
}

std::string Color::toHex(const bool includeAlpha) const {
    char buffer[10];
    if (includeAlpha) {
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X",
                      static_cast<unsigned>(r), static_cast<unsigned>(g),
                      static_cast<unsigned>(b), static_cast<unsigned>(a));
    } else {
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X",
                      static_cast<unsigned>(r), static_cast<unsigned>(g), static_cast<unsigned>(b));
    }
    return buffer;
}

Color Color::mix(const Color& other, float t) const noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    const auto blend = [t](const std::uint8_t from, const std::uint8_t to) {
        return toByte(static_cast<float>(from) + (static_cast<float>(to) - static_cast<float>(from)) * t);
    };
    return Color(blend(r, other.r), blend(g, other.g), blend(b, other.b), blend(a, other.a));
}

Color Color::lighten(const float amount) const noexcept {
    return mix(Color(255, 255, 255, a), amount);
}

Color Color::darken(const float amount) const noexcept {
    return mix(Color(0, 0, 0, a), amount);
}

float Color::luminance() const noexcept {
    return 0.2126f * toLinear(r) + 0.7152f * toLinear(g) + 0.0722f * toLinear(b);
}

float Color::hue() const noexcept {
    const float red   = static_cast<float>(r) / 255.0f;
    const float green = static_cast<float>(g) / 255.0f;
    const float blue  = static_cast<float>(b) / 255.0f;

    const float highest = std::max({red, green, blue});
    const float lowest  = std::min({red, green, blue});
    const float delta   = highest - lowest;
    if (delta <= 0.0f) return 0.0f;

    float hue;
    if (highest == red) {
        hue = std::fmod((green - blue) / delta, 6.0f);
    } else if (highest == green) {
        hue = (blue - red) / delta + 2.0f;
    } else {
        hue = (red - green) / delta + 4.0f;
    }

    hue *= 60.0f;
    return hue < 0.0f ? hue + 360.0f : hue;
}

float Color::contrastRatio(const Color& first, const Color& second) noexcept {
    const float a = first.luminance();
    const float b = second.luminance();
    const float lighter = std::max(a, b);
    const float darker  = std::min(a, b);
    return (lighter + 0.05f) / (darker + 0.05f);
}

} // namespace ui
