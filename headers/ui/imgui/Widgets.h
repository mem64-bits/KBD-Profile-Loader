#pragma once

#include "ui/ActuationModel.h"
#include "ui/AppState.h"
#include "ui/GamepadModel.h"
#include "ui/KeyboardLayout.h"
#include "ui/Theme.h"

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ui::widgets {

    // Rounded label drawn inline (optionally with a status dot in front); the cursor moves past it, so
    // call ImGui::SameLine() to put the next item beside it. Returns the size it occupies.
    ImVec2 pill(const char* text, const Color& fill, const Color& textColor, const std::optional<Color>& dot = std::nullopt);
    [[nodiscard]] float pillHeight();

    // Width of a button with the given label (text plus its horizontal padding).
    [[nodiscard]] float buttonWidth(const char* label);

    // Small muted caption on the left and a value in the accent colour right-aligned on the same line.
    void labelWithValue(const char* label, const std::string& value, const ThemePalette& palette);

    // Small muted heading above a group of controls.
    void sectionLabel(const char* text, const ThemePalette& palette);

    // Accent-filled button (the call to action) and an outlined, quieter one.
    bool primaryButton(const char* label, const ImVec2& size, const ThemePalette& palette);
    bool secondaryButton(const char* label, const ImVec2& size, const ThemePalette& palette);

    // Extras for slider(): tick dots, warning-coloured markers, the value bubble's format.
    struct SliderMarks {
        float              every{0.0f};        // a dot every `every` units (0: none)
        std::vector<float> markers;            // vertical marks at these values
        const char*        format{nullptr};    // printf format of the bubble shown while hovered / dragged
        float              displayScale{1.0f}; // value * scale is what the bubble shows
        bool               fillFromRight{false};
    };

    // A modern slider: thin rounded track with a gradient fill, a round knob that grows under the mouse and a
    // value bubble. Drag, click the track, use the mouse wheel or the arrow keys (when focused). Returns true
    // when the value changed.
    bool slider(const char* id, float& value, float minimum, float maximum, float step, float width, const ThemePalette& palette,
                const SliderMarks& marks = {});

    // slider() over tenths of a millimetre (one step = 0.1 mm), with a dot every 0.5 mm.
    bool millimetreSlider(const char* id, std::uint8_t& tenths, std::uint8_t minimum, std::uint8_t maximum, float width,
                          const ThemePalette& palette, const std::vector<float>& markers = {});

    // An on / off switch with an optional label after it ("##id" hides the label). Returns true when clicked.
    bool toggle(const char* label, bool& value, const ThemePalette& palette);

    // Horizontal 0 - 4 mm key-travel bar with the actuation point marked.
    void travelDiagram(float width, std::uint8_t actuationTenths, const ThemePalette& palette);

    // Rotating arc shown while the keyboard is busy.
    void spinner(float radius, const Color& color);

    // A bordered card on the surface colour that grows with its content. Always pair with endCard().
    bool beginCard(const char* id, const ThemePalette& palette);
    void endCard();

    // Wrapped text in the muted colour.
    void mutedWrapped(const char* text, const ThemePalette& palette);

    // Moves the cursor so that an item `width` wide ends at the right edge of the current line.
    void alignRight(float width);

    // A row of joined toggle buttons (the selected one filled). Returns the index clicked, or -1.
    int segmented(const char* id, std::span<const char* const> labels, int selected, const ThemePalette& palette, float width = 0.0f);

    // "Slot 1  Slot 2 *  Slot 3" picker; * marks the slot active on the keyboard. Returns the slot clicked.
    std::optional<std::size_t> slotButtons(const AppSnapshot& state, std::size_t selected, const ThemePalette& palette);

    // The layout to draw: the user's pick, else the one the connected keyboard reports (ANSI / ISO), else ISO.
    [[nodiscard]] LayoutKind layoutFor(const AppSnapshot& state, std::optional<LayoutKind> picked) noexcept;

    // Keycap name for messages: the legend, "Up" for arrow keys, "Space" for the space bar.
    [[nodiscard]] const char* keyName(const KeyGeometry& key) noexcept;
    [[nodiscard]] std::string keyName(const KeyPosition& key, LayoutKind layout);

    // Line icons for the navigation, drawn with the draw list (no icon font needed).
    enum class Icon : std::uint8_t { Profiles, Actuation, Movement, Gamepad, Settings };
    void drawIcon(ImDrawList* draw, Icon icon, ImVec2 centre, float size, ImU32 color, float thickness);

    // A controller face button: a filled circle with its letter (Xbox labels) or its shape (PlayStation).
    void drawFaceButton(ImDrawList* draw, gamepad::Button button, gamepad::LabelStyle style, ImVec2 centre, float radius,
                        ImU32 fill, ImU32 symbol, ImU32 outline = 0);
    // Just the symbol (letter or shape) of a face button, for small chips.
    void drawFaceSymbol(ImDrawList* draw, gamepad::Button button, gamepad::LabelStyle style, ImVec2 centre, float radius, ImU32 color);

    // An arrow pointing in a numpad direction (8 = up, 3 = down-right); 5 draws a dot.
    void drawDirection(ImDrawList* draw, int numpad, ImVec2 centre, float radius, ImU32 color);

} // namespace ui::widgets
