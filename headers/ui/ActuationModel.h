#pragma once

#include "kbd/ProfileMemory.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace ui {

    namespace actuation {

        // Limits enforced by the Keychron firmware (analog_matrix/profile.c): an actuation point
        // above 3.9 mm is rejected and 0 means "use the default"; sensitivities are 0.1 - 3.9 mm.
        inline constexpr std::uint8_t MIN_TENTHS                 = 1;
        inline constexpr std::uint8_t MAX_TENTHS                 = 39;
        inline constexpr std::uint8_t TOTAL_TRAVEL_TENTHS        = 40; // 4.0 mm of key travel
        inline constexpr std::uint8_t DEFAULT_ACTUATION_TENTHS   = 20; // firmware default: 2.0 mm
        inline constexpr std::uint8_t DEFAULT_SENSITIVITY_TENTHS = 4;  // firmware default: 0.4 mm

    } // namespace actuation

    // What the user can tune for a whole keyboard slot: static actuation point, or Rapid Trigger with
    // separate press / release sensitivities. All distances are in tenths of a millimetre, which is the
    // unit of the HID protocol, so a slider position maps 1:1 onto the byte sent to the keyboard.
    struct ActuationSettings {
        bool         rapidTrigger{false};
        std::uint8_t actuationTenths{actuation::DEFAULT_ACTUATION_TENTHS};
        std::uint8_t pressTenths{actuation::DEFAULT_SENSITIVITY_TENTHS};
        std::uint8_t releaseTenths{actuation::DEFAULT_SENSITIVITY_TENTHS};

        bool operator==(const ActuationSettings&) const noexcept = default;
    };

    namespace actuation {

        [[nodiscard]] float toMillimetres(std::uint8_t tenths) noexcept;
        [[nodiscard]] std::uint8_t clampTenths(int tenths) noexcept;
        [[nodiscard]] std::uint8_t fromMillimetres(float millimetres) noexcept; // rounds, then clamps

        // Same values with every distance clamped into the range the firmware accepts.
        [[nodiscard]] ActuationSettings sanitized(const ActuationSettings& settings) noexcept;

        // Interprets a profile's global config the way the firmware does when it loads one
        // (profile_init: 0 / out-of-range values fall back to the defaults).
        [[nodiscard]] ActuationSettings fromGlobalTravel(const kbd::GlobalTravelConfig& config) noexcept;

        [[nodiscard]] std::string formatMillimetres(std::uint8_t tenths);   // "0.3 mm"
        [[nodiscard]] std::string describe(const ActuationSettings& settings); // "0.3 mm, Rapid Trigger 0.1 mm / 0.1 mm"

        struct Preset {
            std::string_view  name;
            ActuationSettings settings;
        };

        [[nodiscard]] std::span<const Preset> presets() noexcept;

    } // namespace actuation

} // namespace ui
