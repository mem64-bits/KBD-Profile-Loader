#pragma once

#include "kbd/GameController.h"
#include "ui/ActuationModel.h"
#include "ui/GamepadModel.h"
#include "ui/KeyboardLayout.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// What the gamepad and movement pages offer: names for every game-controller target a key can drive, ready-made
// key mappings, the SOCD resolution modes and the keycodes the "sprint on deep press" keystroke can add. Pure
// data and lookups, no ImGui, so it is unit-tested.
namespace ui::mapping {

    enum class TargetGroup : std::uint8_t {
        LeftStick,
        RightStick,
        Triggers,
        FaceButtons,
        Shoulders,
        Dpad,
        Menu,        // View / Menu / Guide
        StickClicks,
        HidOnly,     // dial and buttons 16-32: only the generic HID joystick mode has them
    };

    // A game-controller target (kbd::GamepadTarget) as the UI shows it.
    struct TargetInfo {
        std::uint8_t                  target{0};
        const char*                   name{""};       // "Left stick up", "A", "D-pad left"
        const char*                   chip{""};       // short keycap label: "LS", "LT", "A", "LB", "D"
        KeyGlyph                      glyph{KeyGlyph::None}; // direction drawn after the chip text
        TargetGroup                   group{TargetGroup::HidOnly};
        std::optional<gamepad::Button> button;        // the controller button it presses, if any
        std::optional<gamepad::Axis>   axis;          // ... or the axis it moves
        int                           axisSign{0};    // +1 / -1 on that axis (model convention: +y = up)
    };

    // Every target, in a stable order (sticks, triggers, face buttons, ...).
    [[nodiscard]] std::span<const TargetInfo> targets() noexcept;
    [[nodiscard]] const TargetInfo* targetInfo(std::uint8_t target) noexcept;
    // Name with the face buttons and menu buttons in the chosen style ("Cross" instead of "A").
    [[nodiscard]] std::string targetName(std::uint8_t target, gamepad::LabelStyle style);
    [[nodiscard]] const char* groupName(TargetGroup group) noexcept;

    // A ready-made set of key -> target bindings. Applying it only touches the keys it lists.
    struct Preset {
        const char*                                       name;
        const char*                                       description;
        std::vector<std::pair<KeyPosition, std::uint8_t>> bindings;
    };
    [[nodiscard]] const std::vector<Preset>& presets();

    // SOCD resolution modes (socd_config_t.type, SOCD_PRI_*), with the Keychron Launcher's names alongside.
    struct SocdMode {
        std::uint8_t type;
        const char*  name;
        const char*  launcherName;
        const char*  description;
    };
    [[nodiscard]] std::span<const SocdMode> socdModes() noexcept;
    [[nodiscard]] const SocdMode* socdMode(std::uint8_t type) noexcept;

    // The Launcher shows at most this many SOCD pairs per profile (the firmware stores 20); the app keeps to it
    // so a profile edited here still looks right in the Launcher.
    inline constexpr std::size_t SOCD_PAIR_LIMIT = 5;

    // The keycode a key sends on the K2 HE's default Windows layer (keymaps/keychron/keymap.c, WIN_BASE), used
    // as the "walk" key of a dynamic keystroke. nullopt for keys without a plain keycode (Fn, RGB, ...).
    [[nodiscard]] std::optional<std::uint16_t> defaultKeycode(LayoutKind layout, const KeyPosition& key) noexcept;

    // Keys the sprint keystroke can add on a deep press.
    struct KeycodeOption {
        std::uint16_t code;
        const char*   name;
    };
    [[nodiscard]] std::span<const KeycodeOption> sprintKeys() noexcept;
    [[nodiscard]] std::string keycodeName(std::uint16_t code);

    // Does `config` look like a "sprint on deep press" keystroke (walk key on the shallow point, a second key
    // held while deep)? Used to show and edit existing ones.
    [[nodiscard]] bool isDeepPressCombo(const kbd::OkmcConfig& config) noexcept;

} // namespace ui::mapping
