#pragma once

#include "ui/ActuationModel.h"
#include "ui/GamepadModel.h"
#include "ui/KeyboardLayout.h"
#include "ui/Theme.h"

#include <imgui.h>

#include <functional>
#include <cstdint>
#include <optional>
#include <set>
#include <string>

namespace ui {

    // A label chip drawn at the bottom of a keycap instead of the actuation value (gamepad target, SOCD pair,
    // dynamic keystroke).
    struct KeyBadge {
        std::string                    text;                       // "LS", "A", "DKS", "1"
        KeyGlyph                       glyph{KeyGlyph::None};      // direction arrow after the text
        Color                          fill;
        Color                          textColor;
        std::optional<gamepad::Button> faceButton;                 // draw this button's symbol instead of the text
        gamepad::LabelStyle            style{gamepad::LabelStyle::Xbox};
        std::optional<std::uint8_t>    prompt;                     // kbd::GamepadTarget: draw its prompt glyph
        bool                           zacksly{false};             // from the Zacksly set instead of Kenney

        bool operator==(const KeyBadge&) const = default;
    };

    // What one key shows: the values it really uses and whether they are its own or the slot's; pages other
    // than Actuation replace the value chip with a badge and tint the keycap.
    struct KeyDisplay {
        ActuationSettings       effective;
        bool                    ownSettings{false};
        std::optional<KeyBadge> badge;      // replaces the actuation chip (and the Rapid Trigger dot)
        std::optional<Color>    tint;       // keycap tint instead of the actuation heat colour
        bool                    dim{false}; // fade keys that do not matter on this page
        bool                    hideValue{false}; // no actuation chip / tint (pages that are not about actuation)
        float                   level{-1};  // 0 .. 1: live key travel drawn as a bar on the keycap (< 0: none)
        std::string             note;       // tooltip text instead of the actuation description
    };

    // Draws a keyboard layout as keycaps (Dear ImGui draw list, no textures) and handles selection:
    //   click            select one key            Ctrl+click   add / remove a key
    //   drag             box-select                Shift / Ctrl + drag adds to the selection
    //   double-click     select every key with the same settings
    //   click empty area / Esc   clear the selection
    // Every key is tinted by its actuation point (shallow = strong accent colour); keys with their own settings
    // carry a filled value chip, keys that follow the slot an outlined one. Stateless apart from the drag.
    class KeyboardView {
    public:
        using DisplayFn = std::function<KeyDisplay(const KeyPosition&)>;

        struct Result {
            bool                       selectionChanged{false};
            std::optional<KeyPosition> hovered;
        };

        Result draw(const char* id, const KeyboardLayout& layout, const DisplayFn& display,
                    std::set<KeyPosition>& selection, const ThemePalette& palette, float width);

        // Colour for an actuation point on the shared scale (also used by the legend).
        [[nodiscard]] static Color heatColor(std::uint8_t actuationTenths, const ThemePalette& palette) noexcept;

        // The colour scale with its end labels, `width` wide.
        static void drawHeatLegend(const ThemePalette& palette, float width);

    private:
        bool   m_dragging{false};
        bool   m_pressedOnBoard{false};
        ImVec2 m_dragStart{};
    };

} // namespace ui
