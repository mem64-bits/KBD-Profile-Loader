#pragma once

#include "kbd/ProfileMemory.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
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

    // Size of the key matrix the firmware addresses (6 rows x 16 columns on the K2 HE).
    inline constexpr std::size_t MATRIX_ROWS = kbd::ProfileMemory::MATRIX_ROWS;
    inline constexpr std::size_t MATRIX_COLS = kbd::ProfileMemory::MATRIX_COLS;

    // A key by its matrix position (not its place on the keycap layout).
    struct KeyPosition {
        std::uint8_t row{0};
        std::uint8_t col{0};

        auto operator<=>(const KeyPosition&) const noexcept = default;
    };

    // One key's own travel settings, exactly as the keyboard stores them. Every field that is 0 means
    // "use the slot's global value" (the firmware's rule), so a default-constructed key follows the slot.
    struct KeyActuation {
        static constexpr std::uint8_t MODE_FOLLOW_GLOBAL = 0;
        static constexpr std::uint8_t MODE_REGULAR       = 1;
        static constexpr std::uint8_t MODE_RAPID         = 2;

        std::uint8_t mode{MODE_FOLLOW_GLOBAL};
        std::uint8_t actuationTenths{0};
        std::uint8_t pressTenths{0};
        std::uint8_t releaseTenths{0};

        [[nodiscard]] bool followsGlobal() const noexcept {
            return mode == MODE_FOLLOW_GLOBAL && actuationTenths == 0 && pressTenths == 0 && releaseTenths == 0;
        }

        bool operator==(const KeyActuation&) const noexcept = default;
    };

    // The per-key settings of one slot, indexed by matrix position.
    struct KeyMatrix {
        std::array<KeyActuation, MATRIX_ROWS * MATRIX_COLS> keys{};

        [[nodiscard]] const KeyActuation& at(const KeyPosition& key) const noexcept { return keys[index(key)]; }
        [[nodiscard]] KeyActuation&       at(const KeyPosition& key) noexcept { return keys[index(key)]; }
        [[nodiscard]] std::size_t         overrideCount() const noexcept; // keys that exist on the board only

        bool operator==(const KeyMatrix&) const noexcept = default;

        // Position in `keys` (also used by other per-key tables).
        [[nodiscard]] static std::size_t index(const KeyPosition& key) noexcept {
            return (static_cast<std::size_t>(key.row) % MATRIX_ROWS) * MATRIX_COLS + (key.col % MATRIX_COLS);
        }
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

        // --- per key -------------------------------------------------------------------------------------

        // What the key really does: the key's own values where it has them, the slot's otherwise
        // (analog_matrix.c: update_key_config).
        [[nodiscard]] ActuationSettings effective(const ActuationSettings& global, const KeyActuation& key) noexcept;

        // A key override that pins every value (mode, actuation and both sensitivities).
        [[nodiscard]] KeyActuation overrideFrom(const ActuationSettings& settings) noexcept;

        // Distances clamped to 0 (follow global) or the firmware range, mode to 0..2.
        [[nodiscard]] KeyActuation sanitized(const KeyActuation& key) noexcept;

        // "follows the slot" or e.g. "0.3 mm, Rapid Trigger 0.1 mm / 0.1 mm".
        [[nodiscard]] std::string describe(const KeyActuation& key, const ActuationSettings& global);

        // Per-key part of an 860-byte profile dump; nullopt when the dump is too short.
        [[nodiscard]] std::optional<KeyMatrix> keyMatrixFromProfile(std::span<const std::uint8_t> profile) noexcept;

        struct Preset {
            std::string_view  name;
            ActuationSettings settings;
        };

        [[nodiscard]] std::span<const Preset> presets() noexcept;

    } // namespace actuation

} // namespace ui
