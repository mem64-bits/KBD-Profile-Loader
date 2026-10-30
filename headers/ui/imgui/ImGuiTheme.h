#pragma once

#include "ui/AppState.h"
#include "ui/Color.h"
#include "ui/Theme.h"

#include <imgui.h>

namespace ui {

    [[nodiscard]] ImVec4 toImVec4(const Color& color) noexcept;
    [[nodiscard]] ImU32 toImU32(const Color& color) noexcept;

    // Colour of a status message of the given severity (info = accent, success / warning / error = semantic colours).
    [[nodiscard]] Color statusColor(const ThemePalette& palette, StatusSeverity severity) noexcept;

    // The only place that knows how ui::Theme maps onto ImGui's global style. Call it again whenever the
    // theme or the window's DPI scale changes; it starts from ImGui's defaults every time.
    void applyImGuiTheme(const Theme& theme, float dpiScale);

} // namespace ui
