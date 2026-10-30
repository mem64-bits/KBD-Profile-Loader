#pragma once

#include "ui/ActuationModel.h"
#include "ui/Theme.h"

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <string>

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

    // Slider over tenths of a millimetre (one slider step = 0.1 mm). Returns true when it changed.
    bool millimetreSlider(const char* id, std::uint8_t& tenths, std::uint8_t minimum, std::uint8_t maximum, float width);

    // Horizontal 0 - 4 mm key-travel bar with the actuation point marked.
    void travelDiagram(float width, std::uint8_t actuationTenths, const ThemePalette& palette);

    // Rotating arc shown while the keyboard is busy.
    void spinner(float radius, const Color& color);

} // namespace ui::widgets
