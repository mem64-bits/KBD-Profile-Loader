#pragma once

#include "ui/Color.h"

#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <vector>

namespace ui {

    // Semantic colour roles. Views never hard-code colours: they ask the active Theme, so
    // changing a value here (or in theme.json) restyles the whole application.
    // The defaults are the built-in "Black & Purple" look: black surfaces, purple accents.
    struct ThemePalette {
        Color background   {0x0A, 0x0A, 0x0F};  // window background (near black)
        Color surface      {0x12, 0x12, 0x1A};  // cards / panels
        Color surfaceRaised{0x1B, 0x1B, 0x26};  // inputs, popups, hovered rows
        Color border       {0x2E, 0x26, 0x50};  // muted purple outline
        Color text         {0xEC, 0xEA, 0xF5};
        Color textMuted    {0x8C, 0x88, 0xA3};
        Color textOnAccent {0xFF, 0xFF, 0xFF};  // text drawn on top of `accent`
        Color accent       {0x7C, 0x3A, 0xED};  // the purple secondary colour
        Color accentHover  {0x8B, 0x5C, 0xF6};
        Color accentActive {0x6D, 0x28, 0xD9};
        Color accentText   {0xA7, 0x8B, 0xFA};  // light purple for text / outlines on black
        Color success      {0x34, 0xD3, 0x99};
        Color warning      {0xFB, 0xBF, 0x24};
        Color danger       {0xF8, 0x71, 0x71};

        bool operator==(const ThemePalette&) const noexcept = default;
    };

    // Sizes in logical pixels (scaled by the window's DPI at runtime).
    struct ThemeMetrics {
        float windowRounding  {10.0f};
        float frameRounding   {6.0f};
        float popupRounding   {8.0f};
        float tabRounding     {6.0f};
        float scrollbarRounding{9.0f};
        float windowBorderSize{1.0f};
        float frameBorderSize {0.0f};
        float windowPaddingX  {16.0f};
        float windowPaddingY  {14.0f};
        float framePaddingX   {10.0f};
        float framePaddingY   {6.0f};
        float itemSpacingX    {10.0f};
        float itemSpacingY    {9.0f};
        float fontSize        {16.0f};

        bool operator==(const ThemeMetrics&) const noexcept = default;
    };

    // Something wrong (but recoverable) found while reading a theme file.
    struct ThemeIssue {
        std::string field;
        std::string message;
    };

    struct Theme {
        std::string  name{"Black & Purple"};
        std::string  fontFile;   // optional .ttf; empty = the system UI font, else ImGui's built-in font
        ThemePalette palette{};
        ThemeMetrics metrics{};

        [[nodiscard]] static Theme blackPurple();

        // Builds a theme from JSON. Missing keys keep the Black & Purple default, so a file may
        // override a single colour. If only "accent" is given, the hover/active shades are derived
        // from it. Invalid values are reported through `issues` and ignored. Returns nullopt only
        // when the document is not a JSON object.
        [[nodiscard]] static std::optional<Theme> fromJson(const nlohmann::json& json,
                                                           std::vector<ThemeIssue>* issues = nullptr);

        [[nodiscard]] nlohmann::json toJson() const;

        bool operator==(const Theme&) const noexcept = default;
    };

} // namespace ui
