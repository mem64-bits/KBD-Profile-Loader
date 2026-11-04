#pragma once

#include "ui/AppState.h"
#include "ui/GamepadModel.h"
#include "ui/KeyboardLayout.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <optional>

namespace ui {

    // How controller buttons are pictured on mapped keys: Kenney's CC0 prompts, Zacksly's CC BY 3.0 Xbox Series
    // prompts, or the app's own text chips.
    enum class GlyphStyle : std::uint8_t { Kenney, Zacksly, Drawn };

    // What every page edits, chosen once for the whole window: the slot (picked in the page header), the
    // keyboard drawing and how controller buttons are named (both in Settings). Pages hold a shared pointer to
    // one Workspace, so switching pages keeps the context. UI thread only.
    struct Workspace {
        std::optional<std::size_t>         pickedSlot; // nullopt: follow the slot active on the keyboard
        std::optional<LayoutKind>          layout;     // nullopt: from the connected keyboard (ANSI / ISO)
        std::optional<gamepad::LabelStyle> labels;     // nullopt: from the connected controller
        GlyphStyle                         glyphs{GlyphStyle::Kenney};
        std::array<bool, SLOT_COUNT>       detailsRequested{};  // auto read-back asked for once per slot
        std::array<bool, SLOT_COUNT>       actuationRequested{};

        [[nodiscard]] std::size_t slot(const AppSnapshot& state) const noexcept {
            if (pickedSlot && *pickedSlot < SLOT_COUNT) return *pickedSlot;
            return state.activeSlot >= 0 && state.activeSlot < static_cast<int>(SLOT_COUNT) ? static_cast<std::size_t>(state.activeSlot) : 0;
        }

        [[nodiscard]] LayoutKind keyboardLayout(const AppSnapshot& state) const noexcept {
            if (layout) return *layout;
            return state.device.variant == kbd::KeyboardVariant::Ansi ? LayoutKind::Ansi : LayoutKind::Iso;
        }

        [[nodiscard]] gamepad::LabelStyle labelStyle() const noexcept { return labels.value_or(gamepad::LabelStyle::Xbox); }
    };

    // Number of keys a slot maps to the game controller (0: a keyboard-only profile).
    [[nodiscard]] inline std::size_t gamepadKeyCount(const SlotDetails& details) {
        return static_cast<std::size_t>(std::count_if(details.keys.begin(), details.keys.end(),
                                                      [](const KeyAdvanced& key) { return key.mode == kbd::AdvancedMode::GAMEPAD; }));
    }

} // namespace ui
