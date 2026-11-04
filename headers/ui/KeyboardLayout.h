#pragma once

#include "ui/ActuationModel.h"

#include <cstdint>
#include <span>

namespace ui {

    // Keycap colour family, as Keychron's VIA definitions colour them (alphas, modifiers, accent keys).
    enum class KeyRole : std::uint8_t { Alpha, Modifier, Accent };

    // Legends drawn as shapes instead of text (the UI font may not have arrow glyphs).
    enum class KeyGlyph : std::uint8_t { None, ArrowUp, ArrowDown, ArrowLeft, ArrowRight };

    // One key: its matrix position and where it sits on the board, in key units (1U = one standard key pitch).
    // Stepped keys (ISO Enter) have a second rectangle: x2/y2 are relative to x/y, as in KLE / VIA layouts.
    struct KeyGeometry {
        std::uint8_t row;
        std::uint8_t col;
        float        x;
        float        y;
        float        w;
        float        h;
        float        x2;
        float        y2;
        float        w2;
        float        h2;
        const char*  legend;      // main legend ("A", "Shift"); empty for the space bar and glyph keys
        const char*  shiftLegend; // upper legend on number / symbol keys ("!", "£"), else empty
        KeyRole      role;
        KeyGlyph     glyph;

        [[nodiscard]] constexpr bool        isStepped() const noexcept { return w2 > 0.0f; }
        [[nodiscard]] constexpr KeyPosition position() const noexcept { return KeyPosition{row, col}; }
    };

    enum class LayoutKind : std::uint8_t { Iso, Ansi };

    struct KeyboardLayout {
        LayoutKind                   kind;
        const char*                  name;   // "ISO (UK)"
        float                        width;  // in key units
        float                        height;
        std::span<const KeyGeometry> keys;

        // The key at a matrix position, or nullptr when the layout has no key there.
        [[nodiscard]] const KeyGeometry* find(const KeyPosition& position) const noexcept;
    };

    // Keychron K2 HE layouts. Geometry and matrix positions come from Keychron's open-source firmware
    // (github.com/Keychron/qmk_firmware, keyboards/keychron/k2_he/{iso,ansi}/keyboard.json and via_json/).
    [[nodiscard]] const KeyboardLayout& keyboardLayout(LayoutKind kind) noexcept;
    [[nodiscard]] std::span<const LayoutKind> availableLayouts() noexcept;

    // True when a K2 HE (ISO or ANSI) has a key at this matrix position. 11 of the 96 positions are unused;
    // the Launcher leaves them at defaults, so they must not count as keys "with their own settings".
    [[nodiscard]] bool isKeyOnBoard(const KeyPosition& position) noexcept;

} // namespace ui
