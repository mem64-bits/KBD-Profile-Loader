#include "ui/ControllerMapping.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace ui::mapping {

namespace {

    using kbd::GamepadTarget::button;
    using gamepad::Axis;
    using gamepad::Button;
    namespace T = kbd::GamepadTarget;

    // kbd::GamepadTarget -> what it means. Button numbers follow action_xinput.c (0 A, 1 B, 2 X, 3 Y, 4 LB,
    // 5 RB, 6 Back, 7 Start, 8 / 9 stick clicks, 10 - 13 D-pad, 14 Guide).
    const std::array<TargetInfo, 27> TARGETS{{
        {T::LEFT_STICK_UP, "Left stick up", "LS", KeyGlyph::ArrowUp, TargetGroup::LeftStick, std::nullopt, Axis::LeftY, +1},
        {T::LEFT_STICK_DOWN, "Left stick down", "LS", KeyGlyph::ArrowDown, TargetGroup::LeftStick, std::nullopt, Axis::LeftY, -1},
        {T::LEFT_STICK_LEFT, "Left stick left", "LS", KeyGlyph::ArrowLeft, TargetGroup::LeftStick, std::nullopt, Axis::LeftX, -1},
        {T::LEFT_STICK_RIGHT, "Left stick right", "LS", KeyGlyph::ArrowRight, TargetGroup::LeftStick, std::nullopt, Axis::LeftX, +1},
        {T::RIGHT_STICK_UP, "Right stick up", "RS", KeyGlyph::ArrowUp, TargetGroup::RightStick, std::nullopt, Axis::RightY, +1},
        {T::RIGHT_STICK_DOWN, "Right stick down", "RS", KeyGlyph::ArrowDown, TargetGroup::RightStick, std::nullopt, Axis::RightY, -1},
        {T::RIGHT_STICK_LEFT, "Right stick left", "RS", KeyGlyph::ArrowLeft, TargetGroup::RightStick, std::nullopt, Axis::RightX, -1},
        {T::RIGHT_STICK_RIGHT, "Right stick right", "RS", KeyGlyph::ArrowRight, TargetGroup::RightStick, std::nullopt, Axis::RightX, +1},
        {T::LEFT_TRIGGER, "Left trigger", "LT", KeyGlyph::None, TargetGroup::Triggers, std::nullopt, Axis::LeftTrigger, +1},
        {T::RIGHT_TRIGGER, "Right trigger", "RT", KeyGlyph::None, TargetGroup::Triggers, std::nullopt, Axis::RightTrigger, +1},
        {button(0), "A", "A", KeyGlyph::None, TargetGroup::FaceButtons, Button::South, std::nullopt, 0},
        {button(1), "B", "B", KeyGlyph::None, TargetGroup::FaceButtons, Button::East, std::nullopt, 0},
        {button(2), "X", "X", KeyGlyph::None, TargetGroup::FaceButtons, Button::West, std::nullopt, 0},
        {button(3), "Y", "Y", KeyGlyph::None, TargetGroup::FaceButtons, Button::North, std::nullopt, 0},
        {button(4), "Left bumper", "LB", KeyGlyph::None, TargetGroup::Shoulders, Button::LeftShoulder, std::nullopt, 0},
        {button(5), "Right bumper", "RB", KeyGlyph::None, TargetGroup::Shoulders, Button::RightShoulder, std::nullopt, 0},
        {button(10), "D-pad up", "D", KeyGlyph::ArrowUp, TargetGroup::Dpad, Button::DpadUp, std::nullopt, 0},
        {button(11), "D-pad down", "D", KeyGlyph::ArrowDown, TargetGroup::Dpad, Button::DpadDown, std::nullopt, 0},
        {button(12), "D-pad left", "D", KeyGlyph::ArrowLeft, TargetGroup::Dpad, Button::DpadLeft, std::nullopt, 0},
        {button(13), "D-pad right", "D", KeyGlyph::ArrowRight, TargetGroup::Dpad, Button::DpadRight, std::nullopt, 0},
        {button(6), "View (Back)", "View", KeyGlyph::None, TargetGroup::Menu, Button::Back, std::nullopt, 0},
        {button(7), "Menu (Start)", "Menu", KeyGlyph::None, TargetGroup::Menu, Button::Start, std::nullopt, 0},
        {button(14), "Guide", "Home", KeyGlyph::None, TargetGroup::Menu, Button::Guide, std::nullopt, 0},
        {button(8), "Left stick click", "L3", KeyGlyph::None, TargetGroup::StickClicks, Button::LeftStick, std::nullopt, 0},
        {button(9), "Right stick click", "R3", KeyGlyph::None, TargetGroup::StickClicks, Button::RightStick, std::nullopt, 0},
        {T::DIAL_MINUS, "Dial down (HID mode)", "Z-", KeyGlyph::None, TargetGroup::HidOnly, std::nullopt, std::nullopt, 0},
        {T::DIAL_PLUS, "Dial up (HID mode)", "Z+", KeyGlyph::None, TargetGroup::HidOnly, std::nullopt, std::nullopt, 0},
    }};

    // HID-joystick-only buttons 16 - 32 (XInput has 15 buttons). Built on first use.
    struct ExtraButton {
        std::array<char, 24> name{};
        std::array<char, 6>  chip{};
    };

    const std::array<ExtraButton, kbd::GamepadTarget::BUTTON_COUNT - 15>& extraButtons() {
        static const auto table = [] {
            std::array<ExtraButton, kbd::GamepadTarget::BUTTON_COUNT - 15> buttons{};
            for (std::size_t i = 0; i < buttons.size(); ++i) {
                std::snprintf(buttons[i].name.data(), buttons[i].name.size(), "Button %zu (HID mode)", i + 16);
                std::snprintf(buttons[i].chip.data(), buttons[i].chip.size(), "B%zu", i + 16);
            }
            return buttons;
        }();
        return table;
    }

    const std::vector<TargetInfo>& allTargets() {
        static const std::vector<TargetInfo> all = [] {
            std::vector<TargetInfo> list(TARGETS.begin(), TARGETS.end());
            const auto& extra = extraButtons();
            for (std::size_t i = 0; i < extra.size(); ++i) {
                list.push_back({button(static_cast<std::uint8_t>(15 + i)), extra[i].name.data(), extra[i].chip.data(), KeyGlyph::None,
                                TargetGroup::HidOnly, std::nullopt, std::nullopt, 0});
            }
            return list;
        }();
        return all;
    }

    // Matrix positions on the K2 HE (the same on ANSI and ISO for every key used below).
    constexpr KeyPosition W{2, 2}, A{3, 1}, S{3, 2}, D{3, 3}, E{2, 3}, R{2, 4};
    constexpr KeyPosition U{2, 7}, I{2, 8}, H{3, 6}, J{3, 7}, K{3, 8}, Y{2, 6};
    constexpr KeyPosition SPACE{5, 6}, LSHIFT{4, 0}, RSHIFT{4, 12}, LCTRL{5, 0}, RCTRL{5, 11}, TAB{2, 0};
    constexpr KeyPosition UP{4, 13}, DOWN{5, 13}, LEFT{5, 12}, RIGHT{5, 14};
    constexpr KeyPosition GRAVE{1, 0}, PGUP{1, 14}, HOME{3, 13}, SEMICOLON{3, 10}, QUOTE{3, 11}, SLASH{4, 11}, LBRACKET{2, 11};
    constexpr KeyPosition NUM1{1, 1}, NUM2{1, 2}, NUM3{1, 3}, NUM4{1, 4};

    const std::array<SocdMode, 6> SOCD_MODES{{
        {1, "Deeper press wins", "Rappy Snappy",
         "Whichever key is pressed further down wins. When both are fully pressed, both count."},
        {2, "Deeper press wins (hold)", "Rappy Snappy",
         "Like the above, but when both are fully pressed the key that was winning keeps winning."},
        {3, "Last input wins", "Last Key Priority",
         "The key pressed most recently wins; releasing it hands control back to the other one (Snap Tap)."},
        {4, "First key always wins", "Absolute Priority",
         "Key 1 always wins while both are held."},
        {5, "Second key always wins", "Absolute Priority",
         "Key 2 always wins while both are held."},
        {6, "Neutral", "Neutral",
         "Both keys cancel out while both are held (tournament rules for left + right)."},
    }};

    const std::array<KeycodeOption, 14> SPRINT_KEYS{{
        {kbd::Keycode::LEFT_SHIFT, "Left Shift"},
        {kbd::Keycode::LEFT_CTRL, "Left Ctrl"},
        {kbd::Keycode::LEFT_ALT, "Left Alt"},
        {kbd::Keycode::SPACE, "Space"},
        {kbd::Keycode::TAB, "Tab"},
        {kbd::Keycode::CAPS_LOCK, "Caps Lock"},
        {0x06, "C"},
        {0x08, "E"},
        {0x09, "F"},
        {0x14, "Q"},
        {0x15, "R"},
        {0x19, "V"},
        {0x1B, "X"},
        {0x1D, "Z"},
    }};

    struct KeycodeEntry {
        std::uint8_t  row;
        std::uint8_t  col;
        std::uint16_t code;
    };

    // Generated from keyboards/keychron/k2_he/{iso,ansi}/keymaps/keychron/keymap.c (WIN_BASE) and keyboard.json.
    constexpr KeycodeEntry ISO_KEYCODES[] = {
        {0, 0, 0x29}, {0, 1, 0x3A}, {0, 2, 0x3B}, {0, 3, 0x3C}, {0, 4, 0x3D}, {0, 5, 0x3E}, {0, 6, 0x3F},
        {0, 7, 0x40}, {0, 8, 0x41}, {0, 9, 0x42}, {0, 10, 0x43}, {0, 11, 0x44}, {0, 12, 0x45}, {0, 13, 0x46},
        {0, 14, 0x4C}, {1, 0, 0x35}, {1, 1, 0x1E}, {1, 2, 0x1F}, {1, 3, 0x20}, {1, 4, 0x21}, {1, 5, 0x22},
        {1, 6, 0x23}, {1, 7, 0x24}, {1, 8, 0x25}, {1, 9, 0x26}, {1, 10, 0x27}, {1, 11, 0x2D}, {1, 12, 0x2E},
        {1, 13, 0x2A}, {1, 14, 0x4B}, {2, 0, 0x2B}, {2, 1, 0x14}, {2, 2, 0x1A}, {2, 3, 0x08}, {2, 4, 0x15},
        {2, 5, 0x17}, {2, 6, 0x1C}, {2, 7, 0x18}, {2, 8, 0x0C}, {2, 9, 0x12}, {2, 10, 0x13}, {2, 11, 0x2F},
        {2, 12, 0x30}, {2, 14, 0x4E}, {3, 0, 0x39}, {3, 1, 0x04}, {3, 2, 0x16}, {3, 3, 0x07}, {3, 4, 0x09},
        {3, 5, 0x0A}, {3, 6, 0x0B}, {3, 7, 0x0D}, {3, 8, 0x0E}, {3, 9, 0x0F}, {3, 10, 0x33}, {3, 11, 0x34},
        {3, 12, 0x32}, {2, 13, 0x28}, {3, 13, 0x4A}, {4, 0, 0xE1}, {4, 1, 0x64}, {4, 2, 0x1D}, {4, 3, 0x1B},
        {4, 4, 0x06}, {4, 5, 0x19}, {4, 6, 0x05}, {4, 7, 0x11}, {4, 8, 0x10}, {4, 9, 0x36}, {4, 10, 0x37},
        {4, 11, 0x38}, {4, 12, 0xE5}, {4, 13, 0x52}, {4, 14, 0x4D}, {5, 0, 0xE0}, {5, 1, 0xE3}, {5, 2, 0xE2},
        {5, 6, 0x2C}, {5, 9, 0xE6}, {5, 11, 0xE4}, {5, 12, 0x50}, {5, 13, 0x51}, {5, 14, 0x4F}
    };
    constexpr KeycodeEntry ANSI_KEYCODES[] = {
        {0, 0, 0x29}, {0, 1, 0x3A}, {0, 2, 0x3B}, {0, 3, 0x3C}, {0, 4, 0x3D}, {0, 5, 0x3E}, {0, 6, 0x3F},
        {0, 7, 0x40}, {0, 8, 0x41}, {0, 9, 0x42}, {0, 10, 0x43}, {0, 11, 0x44}, {0, 12, 0x45}, {0, 13, 0x46},
        {0, 14, 0x4C}, {1, 0, 0x35}, {1, 1, 0x1E}, {1, 2, 0x1F}, {1, 3, 0x20}, {1, 4, 0x21}, {1, 5, 0x22},
        {1, 6, 0x23}, {1, 7, 0x24}, {1, 8, 0x25}, {1, 9, 0x26}, {1, 10, 0x27}, {1, 11, 0x2D}, {1, 12, 0x2E},
        {1, 13, 0x2A}, {1, 14, 0x4B}, {2, 0, 0x2B}, {2, 1, 0x14}, {2, 2, 0x1A}, {2, 3, 0x08}, {2, 4, 0x15},
        {2, 5, 0x17}, {2, 6, 0x1C}, {2, 7, 0x18}, {2, 8, 0x0C}, {2, 9, 0x12}, {2, 10, 0x13}, {2, 11, 0x2F},
        {2, 12, 0x30}, {2, 13, 0x31}, {2, 14, 0x4E}, {3, 0, 0x39}, {3, 1, 0x04}, {3, 2, 0x16}, {3, 3, 0x07},
        {3, 4, 0x09}, {3, 5, 0x0A}, {3, 6, 0x0B}, {3, 7, 0x0D}, {3, 8, 0x0E}, {3, 9, 0x0F}, {3, 10, 0x33},
        {3, 11, 0x34}, {3, 12, 0x28}, {3, 13, 0x4A}, {4, 0, 0xE1}, {4, 2, 0x1D}, {4, 3, 0x1B}, {4, 4, 0x06},
        {4, 5, 0x19}, {4, 6, 0x05}, {4, 7, 0x11}, {4, 8, 0x10}, {4, 9, 0x36}, {4, 10, 0x37}, {4, 11, 0x38},
        {4, 12, 0xE5}, {4, 13, 0x52}, {4, 14, 0x4D}, {5, 0, 0xE0}, {5, 1, 0xE3}, {5, 2, 0xE2}, {5, 6, 0x2C},
        {5, 9, 0xE6}, {5, 11, 0xE4}, {5, 12, 0x50}, {5, 13, 0x51}, {5, 14, 0x4F}
    };

} // namespace

std::span<const TargetInfo> targets() noexcept {
    return allTargets();
}

const TargetInfo* targetInfo(const std::uint8_t target) noexcept {
    const auto& all = allTargets();
    const auto it = std::find_if(all.begin(), all.end(), [target](const TargetInfo& info) { return info.target == target; });
    return it == all.end() ? nullptr : &*it;
}

std::string targetName(const std::uint8_t target, const gamepad::LabelStyle style) {
    const TargetInfo* info = targetInfo(target);
    if (info == nullptr) return "Unknown (" + std::to_string(target) + ")";
    if (info->button && (info->group == TargetGroup::FaceButtons || info->group == TargetGroup::Menu ||
                         info->group == TargetGroup::Shoulders || info->group == TargetGroup::StickClicks)) {
        return gamepad::buttonName(*info->button, style);
    }
    if (style == gamepad::LabelStyle::PlayStation && info->axis == Axis::LeftTrigger) return "L2";
    if (style == gamepad::LabelStyle::PlayStation && info->axis == Axis::RightTrigger) return "R2";
    return info->name;
}

const char* groupName(const TargetGroup group) noexcept {
    switch (group) {
        case TargetGroup::LeftStick:   return "Left stick";
        case TargetGroup::RightStick:  return "Right stick";
        case TargetGroup::Triggers:    return "Triggers";
        case TargetGroup::FaceButtons: return "Face buttons";
        case TargetGroup::Shoulders:   return "Bumpers";
        case TargetGroup::Dpad:        return "D-pad";
        case TargetGroup::Menu:        return "Menu buttons";
        case TargetGroup::StickClicks: return "Stick clicks";
        case TargetGroup::HidOnly:     break;
    }
    return "HID joystick only";
}

const std::vector<Preset>& presets() {
    using namespace kbd::GamepadTarget;
    static const std::vector<Preset> list = {
        {"Analog movement",
         "WASD becomes the left stick: press lightly to walk, all the way to run. For games that accept a controller and "
         "the keyboard together.",
         {{W, LEFT_STICK_UP}, {A, LEFT_STICK_LEFT}, {S, LEFT_STICK_DOWN}, {D, LEFT_STICK_RIGHT}}},
        {"Twin stick",
         "WASD is the left stick, the arrow keys the right stick (camera).",
         {{W, LEFT_STICK_UP}, {A, LEFT_STICK_LEFT}, {S, LEFT_STICK_DOWN}, {D, LEFT_STICK_RIGHT},
          {UP, RIGHT_STICK_UP}, {DOWN, RIGHT_STICK_DOWN}, {LEFT, RIGHT_STICK_LEFT}, {RIGHT, RIGHT_STICK_RIGHT}}},
        {"Racing",
         "W and S are analog throttle and brake (right / left trigger), A and D steer, Space is A, Shift is X.",
         {{W, RIGHT_TRIGGER}, {S, LEFT_TRIGGER}, {A, LEFT_STICK_LEFT}, {D, LEFT_STICK_RIGHT},
          {SPACE, button(0)}, {LSHIFT, button(2)}}},
        {"Full controller",
         "Every stick, trigger and button for emulators: WASD left stick, UHJK right stick, Ctrl keys triggers, "
         "/ ' ; [ face buttons, E and Y bumpers, the arrows the D-pad, ` View, PgUp Menu, Home Guide, Shift keys stick clicks.",
         {{W, LEFT_STICK_UP}, {A, LEFT_STICK_LEFT}, {S, LEFT_STICK_DOWN}, {D, LEFT_STICK_RIGHT},
          {U, RIGHT_STICK_UP}, {H, RIGHT_STICK_LEFT}, {J, RIGHT_STICK_DOWN}, {K, RIGHT_STICK_RIGHT},
          {LCTRL, LEFT_TRIGGER}, {RCTRL, RIGHT_TRIGGER},
          {SLASH, button(0)}, {QUOTE, button(1)}, {SEMICOLON, button(2)}, {LBRACKET, button(3)},
          {E, button(4)}, {Y, button(5)}, {GRAVE, button(6)}, {PGUP, button(7)}, {LSHIFT, button(8)}, {RSHIFT, button(9)},
          {UP, button(10)}, {DOWN, button(11)}, {LEFT, button(12)}, {RIGHT, button(13)}, {HOME, button(14)}}},
        {"Arcade (fight stick)",
         "WASD is the D-pad (digital), U I / J K are the four face buttons, 1-4 the bumpers and triggers as buttons.",
         {{W, button(10)}, {S, button(11)}, {A, button(12)}, {D, button(13)},
          {U, button(2)}, {I, button(3)}, {J, button(0)}, {K, button(1)},
          {NUM1, button(4)}, {NUM2, button(5)}, {NUM3, LEFT_TRIGGER}, {NUM4, RIGHT_TRIGGER},
          {TAB, button(6)}, {R, button(7)}}},
    };
    return list;
}

std::span<const SocdMode> socdModes() noexcept {
    return SOCD_MODES;
}

const SocdMode* socdMode(const std::uint8_t type) noexcept {
    const auto it = std::find_if(SOCD_MODES.begin(), SOCD_MODES.end(), [type](const SocdMode& mode) { return mode.type == type; });
    return it == SOCD_MODES.end() ? nullptr : &*it;
}

std::optional<std::uint16_t> defaultKeycode(const LayoutKind layout, const KeyPosition& key) noexcept {
    const std::span<const KeycodeEntry> table = layout == LayoutKind::Ansi ? std::span<const KeycodeEntry>(ANSI_KEYCODES)
                                                                           : std::span<const KeycodeEntry>(ISO_KEYCODES);
    const auto it = std::find_if(table.begin(), table.end(), [&](const KeycodeEntry& e) { return e.row == key.row && e.col == key.col; });
    if (it == table.end()) return std::nullopt;
    return it->code;
}

std::span<const KeycodeOption> sprintKeys() noexcept {
    return SPRINT_KEYS;
}

std::string keycodeName(const std::uint16_t code) {
    const auto it = std::find_if(SPRINT_KEYS.begin(), SPRINT_KEYS.end(), [code](const KeycodeOption& o) { return o.code == code; });
    if (it != SPRINT_KEYS.end()) return it->name;
    if (code >= 0x04 && code <= 0x1D) return std::string(1, static_cast<char>('A' + (code - 0x04)));
    if (code >= 0x1E && code <= 0x26) return std::string(1, static_cast<char>('1' + (code - 0x1E)));
    switch (code) {
        case 0x27: return "0";
        case 0x28: return "Enter";
        case 0x29: return "Esc";
        case 0x2A: return "Backspace";
        case kbd::Keycode::RIGHT: return "Right";
        case kbd::Keycode::LEFT:  return "Left";
        case kbd::Keycode::DOWN:  return "Down";
        case kbd::Keycode::UP:    return "Up";
        case 0xE3: return "Left Win";
        case 0xE4: return "Right Ctrl";
        case 0xE5: return "Right Shift";
        case 0xE6: return "Right Alt";
        default: break;
    }
    char hex[16];
    std::snprintf(hex, sizeof(hex), "0x%02X", code);
    return hex;
}

bool isDeepPressCombo(const kbd::OkmcConfig& config) noexcept {
    using kbd::OkmcAction;
    if (config.keycodes[0] == 0 || config.keycodes[1] == 0 || config.keycodes[2] != 0 || config.keycodes[3] != 0) return false;
    const OkmcAction& walk = config.actions[0];
    const OkmcAction& run  = config.actions[1];
    return walk.shallowPress == OkmcAction::PRESS && walk.deepPress == OkmcAction::NONE && run.shallowPress == OkmcAction::NONE &&
           run.deepPress == OkmcAction::PRESS && run.deepRelease == OkmcAction::RELEASE;
}

} // namespace ui::mapping
