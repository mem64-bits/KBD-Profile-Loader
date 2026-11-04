#include "ui/ActuationModel.h"
#include "ui/KeyboardLayout.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace ui {

std::size_t KeyMatrix::overrideCount() const noexcept {
    std::size_t count = 0;
    for (std::uint8_t row = 0; row < MATRIX_ROWS; ++row) {
        for (std::uint8_t col = 0; col < MATRIX_COLS; ++col) {
            const KeyPosition key{row, col};
            if (!at(key).followsGlobal() && isKeyOnBoard(key)) ++count;
        }
    }
    return count;
}

} // namespace ui

namespace ui::actuation {

float toMillimetres(const std::uint8_t tenths) noexcept {
    return static_cast<float>(tenths) / 10.0f;
}

std::uint8_t clampTenths(const int tenths) noexcept {
    return static_cast<std::uint8_t>(std::clamp<int>(tenths, MIN_TENTHS, MAX_TENTHS));
}

std::uint8_t fromMillimetres(const float millimetres) noexcept {
    if (!std::isfinite(millimetres)) return DEFAULT_ACTUATION_TENTHS;
    return clampTenths(static_cast<int>(std::lround(millimetres * 10.0f)));
}

ActuationSettings sanitized(const ActuationSettings& settings) noexcept {
    ActuationSettings clean = settings;
    clean.actuationTenths = clampTenths(settings.actuationTenths);
    clean.pressTenths     = clampTenths(settings.pressTenths);
    clean.releaseTenths   = clampTenths(settings.releaseTenths);
    return clean;
}

ActuationSettings fromGlobalTravel(const kbd::GlobalTravelConfig& config) noexcept {
    ActuationSettings settings;
    settings.rapidTrigger = config.isRapidTrigger();

    const bool actuationValid = config.actuationTenths != 0 && config.actuationTenths <= TOTAL_TRAVEL_TENTHS;
    settings.actuationTenths  = clampTenths(actuationValid ? config.actuationTenths : DEFAULT_ACTUATION_TENTHS);

    const bool pressValid = config.pressSensTenths != 0 && config.pressSensTenths <= MAX_TENTHS;
    settings.pressTenths  = clampTenths(pressValid ? config.pressSensTenths : DEFAULT_SENSITIVITY_TENTHS);

    // A release sensitivity of 0 means "same as press" in the firmware.
    settings.releaseTenths = config.releaseSensTenths == 0 ? settings.pressTenths : clampTenths(config.releaseSensTenths);
    return settings;
}

std::string formatMillimetres(const std::uint8_t tenths) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%.1f mm", static_cast<double>(toMillimetres(tenths)));
    return buffer;
}

std::string describe(const ActuationSettings& settings) {
    std::string text = formatMillimetres(settings.actuationTenths);
    if (settings.rapidTrigger) {
        text += ", Rapid Trigger " + formatMillimetres(settings.pressTenths) + " / " + formatMillimetres(settings.releaseTenths);
    } else {
        text += ", Rapid Trigger off";
    }
    return text;
}

ActuationSettings effective(const ActuationSettings& global, const KeyActuation& key) noexcept {
    ActuationSettings result = global;
    if (key.mode != KeyActuation::MODE_FOLLOW_GLOBAL) result.rapidTrigger = key.mode == KeyActuation::MODE_RAPID;
    if (key.actuationTenths != 0) result.actuationTenths = clampTenths(key.actuationTenths);
    if (key.pressTenths != 0) result.pressTenths = clampTenths(key.pressTenths);
    if (key.releaseTenths != 0) result.releaseTenths = clampTenths(key.releaseTenths);
    return result;
}

KeyActuation overrideFrom(const ActuationSettings& settings) noexcept {
    const ActuationSettings clean = sanitized(settings);
    return KeyActuation{clean.rapidTrigger ? KeyActuation::MODE_RAPID : KeyActuation::MODE_REGULAR,
                        clean.actuationTenths, clean.pressTenths, clean.releaseTenths};
}

KeyActuation sanitized(const KeyActuation& key) noexcept {
    const auto distance = [](const std::uint8_t tenths) -> std::uint8_t { return tenths == 0 ? 0 : clampTenths(tenths); };
    return KeyActuation{std::min<std::uint8_t>(key.mode, KeyActuation::MODE_RAPID), distance(key.actuationTenths),
                        distance(key.pressTenths), distance(key.releaseTenths)};
}

std::string describe(const KeyActuation& key, const ActuationSettings& global) {
    if (key.followsGlobal()) return "follows the slot (" + describe(global) + ")";
    return describe(effective(global, key));
}

std::optional<KeyMatrix> keyMatrixFromProfile(const std::span<const std::uint8_t> profile) noexcept {
    if (profile.size() < kbd::ProfileMemory::KEY_CONFIG_OFFSET + kbd::ProfileMemory::KEY_CONFIG_SIZE) return std::nullopt;

    KeyMatrix matrix;
    for (std::size_t row = 0; row < MATRIX_ROWS; ++row) {
        for (std::size_t col = 0; col < MATRIX_COLS; ++col) {
            const auto config = kbd::ProfileMemory::decodeKeyTravel(profile, row, col);
            if (!config) return std::nullopt;
            matrix.at({static_cast<std::uint8_t>(row), static_cast<std::uint8_t>(col)}) =
                KeyActuation{config->mode, config->actuationTenths, config->pressSensTenths, config->releaseSensTenths};
        }
    }
    return matrix;
}

std::span<const Preset> presets() noexcept {
    static constexpr std::array<Preset, 4> PRESETS{{
        {"Firmware default",       {false, 20, 4, 4}},
        {"Fast typing",            {false, 10, 4, 4}},
        {"Gaming - Rapid Trigger", {true,   3, 1, 1}},
        {"Precise Rapid Trigger",  {true,  12, 3, 3}},
    }};
    return PRESETS;
}

} // namespace ui::actuation
