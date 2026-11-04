#pragma once

#include <array>
#include <cstdint>
#include <optional>

// The K2 HE's built-in game controller (keyboards/keychron/common/analog_matrix: game_controller_common.*,
// action_xinput.c, action_joystick.c, branch 2025q3). Keys put in "gamepad" mode drive a stick direction, a
// trigger or a button by how far they are pressed; the keyboard sends that as a real USB controller.
namespace kbd {

    // AMC_GET/SET_GAME_CONTROLLER_MODE: a bit set stored in the keyboard (not per profile). The USB
    // descriptors are chosen when the keyboard enumerates, so a change takes effect after re-plugging.
    namespace ControllerMode {
        inline constexpr std::uint8_t XINPUT = 0x01; // set: Xbox 360-style XInput pad; clear: generic HID joystick
        inline constexpr std::uint8_t TYPING = 0x02; // set: gamepad keys also type their normal key
        inline constexpr std::uint8_t KNOWN_BITS = XINPUT | TYPING;
    }

    // What a gamepad key drives: `key_config.js_axis`, sent as the index of ADV_MODE_GAME_CONTROLLER.
    // Axes come in negative / positive halves; buttons start at 13 (GC_MAX + 1).
    namespace GamepadTarget {
        inline constexpr std::uint8_t LEFT_STICK_LEFT   = 0;
        inline constexpr std::uint8_t LEFT_STICK_RIGHT  = 1;
        inline constexpr std::uint8_t LEFT_STICK_DOWN   = 2;
        inline constexpr std::uint8_t LEFT_STICK_UP     = 3;
        inline constexpr std::uint8_t LEFT_TRIGGER      = 4; // Z-, no response curve
        inline constexpr std::uint8_t RIGHT_TRIGGER     = 5; // Z+
        inline constexpr std::uint8_t RIGHT_STICK_LEFT  = 6;
        inline constexpr std::uint8_t RIGHT_STICK_RIGHT = 7;
        inline constexpr std::uint8_t RIGHT_STICK_DOWN  = 8;
        inline constexpr std::uint8_t RIGHT_STICK_UP    = 9;
        inline constexpr std::uint8_t DIAL_MINUS        = 10; // RZ-, generic HID joystick only
        inline constexpr std::uint8_t DIAL_PLUS         = 11; // RZ+
        inline constexpr std::uint8_t BUTTON_BASE       = 13;
        inline constexpr std::uint8_t BUTTON_COUNT      = 32;
        inline constexpr std::uint8_t END               = BUTTON_BASE + BUTTON_COUNT; // GC_BUTTON_MAX

        // XInput order of the first 16 buttons (action_xinput.c, xinput_buttons_map): 0 A, 1 B, 2 X, 3 Y,
        // 4 LB, 5 RB, 6 Back, 7 Start, 8 left-stick click, 9 right-stick click, 10-13 D-pad up / down / left /
        // right, 14 Guide.
        [[nodiscard]] constexpr std::uint8_t button(const std::uint8_t number) noexcept {
            return static_cast<std::uint8_t>(BUTTON_BASE + number);
        }
        [[nodiscard]] constexpr bool isValid(const std::uint8_t target) noexcept {
            return target < END && target != 12;
        }
        [[nodiscard]] constexpr bool isButton(const std::uint8_t target) noexcept {
            return target >= BUTTON_BASE && target < END;
        }
    }

    // One of the 4 points of the stick response curve: x = key travel in 0.1 mm (0 - 4.0 mm), y = stick
    // deflection 0 - 127. Triggers ignore the curve. The curve is a keyboard-wide setting.
    struct CurvePoint {
        std::uint8_t x{0};
        std::uint8_t y{0};
        bool operator==(const CurvePoint&) const noexcept = default;
    };
    using ResponseCurve = std::array<CurvePoint, 4>;

    inline constexpr std::uint8_t  CURVE_MAX_X = 40;  // FULL_TRAVEL_UNIT: 4.0 mm
    inline constexpr std::uint8_t  CURVE_MAX_Y = 127;
    inline constexpr ResponseCurve DEFAULT_CURVE{{{0, 0}, {10, 31}, {30, 95}, {40, 127}}}; // game_controller_curve_init

    // The firmware checks the order of the curve it already has instead of the new one, so the app must never
    // send an unordered curve: x must not decrease and every value must be in range.
    [[nodiscard]] bool isValidCurve(const ResponseCurve& curve) noexcept;

    // Stick deflection (0 - 127) for a key pressed `travelTenths` deep, the way action_xinput.c computes it:
    // piecewise linear between the points, 0 when the key is up, the last point's value past it.
    [[nodiscard]] float curveOutput(const ResponseCurve& curve, float travelTenths) noexcept;

} // namespace kbd
