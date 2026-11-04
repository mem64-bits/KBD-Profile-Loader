#pragma once

#include "kbd/GameController.h"

#include <array>
#include <cstdint>
#include <span>

// Exact models of two pieces of K2 HE firmware logic (keyboards/keychron/common/analog_matrix, branch 2025q3), so
// the app can show what the keyboard will do before anything is written to it:
//   - XInputModel: action_xinput.c, how gamepad keys become an Xbox 360-style XInput report.
//   - SocdPairModel: action_socd.c, which key of a SOCD pair the keyboard actually sends.
// Both are plain state machines with no I/O, stepped once per "scan" with the current key travel. The firmware's
// integer arithmetic (shifts, truncating casts, the integer square root) is reproduced on purpose: the point is
// to match the keyboard, not to be pretty.
namespace kbd::model {

    // Key travel in the firmware's own unit: TRAVEL_SCALE steps per 0.1 mm, so 0 - 240 covers 0 - 4.0 mm.
    inline constexpr std::uint8_t TRAVEL_SCALE = 6;
    inline constexpr std::uint8_t FULL_TRAVEL  = 40 * TRAVEL_SCALE; // 240

    [[nodiscard]] constexpr std::uint8_t travelFromTenths(const float tenths) noexcept {
        const float t = tenths * TRAVEL_SCALE;
        return t <= 0.0f ? std::uint8_t{0} : t >= 255.0f ? std::uint8_t{255} : static_cast<std::uint8_t>(t + 0.5f);
    }

    // ---------------------------------------------------------------------------------------------------------
    // Key actuation (action_regular_trigger.c, action_rapid_trigger.c)

    // A key's effective travel settings, in 0.1 mm like the rest of the app.
    struct TriggerSettings {
        std::uint8_t actuationTenths{20};
        bool         rapidTrigger{false};
        std::uint8_t pressTenths{3};   // Rapid Trigger: move down this far to press again
        std::uint8_t releaseTenths{3}; // Rapid Trigger: move up this far to release
    };

    // Rapid Trigger never releases a key held below this depth minus the release distance (BOTTOM_DEAD_ZONE).
    inline constexpr std::uint8_t BOTTOM_DEAD_ZONE_TENTHS = 38;
    // A normal key releases 0.3 mm above its actuation point.
    inline constexpr std::uint8_t RELEASE_HYSTERESIS_TENTHS = 3;

    // When one key registers and releases as it moves.
    class KeyTriggerModel {
    public:
        // One scan with the key's current travel; returns true while the key is pressed.
        bool step(std::uint8_t travel, const TriggerSettings& settings);
        void reset() noexcept;

        [[nodiscard]] bool pressed() const noexcept { return m_state == State::RegularPressed || m_state == State::RapidPressed; }
        // Where Rapid Trigger would press / release next (firmware units), for drawing.
        [[nodiscard]] std::uint8_t rapidPressPoint() const noexcept { return m_rapidPress; }
        [[nodiscard]] std::uint8_t rapidReleasePoint() const noexcept { return m_rapidRelease; }

    private:
        enum class State : std::uint8_t { RegularReleased, RegularPressed, RapidReleased, RapidPressed };
        State        m_state{State::RegularReleased};
        std::uint8_t m_rapidPress{0};
        std::uint8_t m_rapidRelease{0};
    };

    // ---------------------------------------------------------------------------------------------------------
    // XInput

    // XInput button bits (XINPUT_GAMEPAD_*).
    namespace XButton {
        inline constexpr std::uint16_t DPAD_UP    = 0x0001;
        inline constexpr std::uint16_t DPAD_DOWN  = 0x0002;
        inline constexpr std::uint16_t DPAD_LEFT  = 0x0004;
        inline constexpr std::uint16_t DPAD_RIGHT = 0x0008;
        inline constexpr std::uint16_t START      = 0x0010;
        inline constexpr std::uint16_t BACK       = 0x0020;
        inline constexpr std::uint16_t LEFT_THUMB = 0x0040;
        inline constexpr std::uint16_t RIGHT_THUMB= 0x0080;
        inline constexpr std::uint16_t LB         = 0x0100;
        inline constexpr std::uint16_t RB         = 0x0200;
        inline constexpr std::uint16_t GUIDE      = 0x0400;
        inline constexpr std::uint16_t A          = 0x1000;
        inline constexpr std::uint16_t B          = 0x2000;
        inline constexpr std::uint16_t X          = 0x4000;
        inline constexpr std::uint16_t Y          = 0x8000;
    }

    // The XInput bit a gamepad button target sets (xinput_buttons_map), 0 for targets without one.
    [[nodiscard]] std::uint16_t xinputButtonBit(std::uint8_t target) noexcept;

    // What Windows' XInputGetState would report. Sticks are -32768..32767 with +y up, triggers 0..255.
    struct XInputReport {
        std::int16_t  leftX{0};
        std::int16_t  leftY{0};
        std::int16_t  rightX{0};
        std::int16_t  rightY{0};
        std::uint8_t  leftTrigger{0};
        std::uint8_t  rightTrigger{0};
        std::uint16_t buttons{0};
        bool operator==(const XInputReport&) const noexcept = default;
    };

    // One gamepad key as the firmware sees it during a scan.
    struct GamepadKey {
        std::uint8_t target{0};    // kbd::GamepadTarget
        std::uint8_t travel{0};    // firmware units (0 - 240)
        bool         pressed{false}; // the key's normal actuation (actuation point / Rapid Trigger) is down
    };

    class XInputModel {
    public:
        // One scan. Keys are processed in the order given (the firmware walks the matrix row by row), so when two
        // keys drive the same target the later one wins, exactly like on the keyboard.
        XInputReport step(std::span<const GamepadKey> keys, const ResponseCurve& curve);
        void reset() noexcept;

    private:
        std::array<std::uint8_t, 12> m_travel{};
        std::array<std::int8_t, 6>   m_dir{};
        std::array<std::int32_t, 6>  m_value{};
        std::uint16_t                m_buttons{0};
    };

    // ---------------------------------------------------------------------------------------------------------
    // SOCD

    // socd_config_t.type (SOCD_PRI_*).
    namespace SocdType {
        inline constexpr std::uint8_t NONE                 = 0;
        inline constexpr std::uint8_t DEEPER_TRAVEL        = 1; // deeper key wins; both bottomed out: both sent
        inline constexpr std::uint8_t DEEPER_TRAVEL_SINGLE = 2; // deeper key wins; both bottomed out: keep the last
        inline constexpr std::uint8_t LAST_KEYSTROKE       = 3;
        inline constexpr std::uint8_t KEY_1                = 4;
        inline constexpr std::uint8_t KEY_2                = 5;
        inline constexpr std::uint8_t NEUTRAL              = 6;
    }

    // Travel above which both keys count as "bottomed out" for the deeper-press modes (3.83 mm).
    inline constexpr std::uint8_t SOCD_BOTTOM_OUT = 230;

    struct SocdKey {
        bool         down{false};  // analog actuation of the key (before SOCD)
        std::uint8_t travel{0};    // firmware units
    };
    struct SocdOutput {
        bool key1{false};
        bool key2{false};
        bool operator==(const SocdOutput&) const noexcept = default;
    };

    // One SOCD pair over time (the firmware keeps a small "which key is active" state per pair).
    class SocdPairModel {
    public:
        SocdOutput step(std::uint8_t type, SocdKey key1, SocdKey key2);
        void reset() noexcept;

    private:
        std::uint8_t m_state{0};    // 1: key 1 active, 2: key 2 active
        SocdOutput   m_previous;    // last scan's output (raw_matrix)
        bool         m_previousDown1{false};
        bool         m_previousDown2{false};
    };

} // namespace kbd::model
