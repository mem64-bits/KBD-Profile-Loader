#include "ui/ActuationModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

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
