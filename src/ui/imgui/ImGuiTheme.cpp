#include "ui/imgui/ImGuiTheme.h"

namespace ui {

ImVec4 toImVec4(const Color& color) noexcept {
    return ImVec4(static_cast<float>(color.r) / 255.0f, static_cast<float>(color.g) / 255.0f,
                  static_cast<float>(color.b) / 255.0f, static_cast<float>(color.a) / 255.0f);
}

ImU32 toImU32(const Color& color) noexcept {
    return IM_COL32(color.r, color.g, color.b, color.a);
}

Color statusColor(const ThemePalette& palette, const StatusSeverity severity) noexcept {
    switch (severity) {
        case StatusSeverity::Success: return palette.success;
        case StatusSeverity::Warning: return palette.warning;
        case StatusSeverity::Error:   return palette.danger;
        case StatusSeverity::Info:    return palette.accentText;
        case StatusSeverity::None:    break;
    }
    return palette.textMuted;
}

void applyImGuiTheme(const Theme& theme, const float dpiScale) {
    const ThemePalette& p = theme.palette;
    const ThemeMetrics& m = theme.metrics;
    const Color transparent = p.background.withAlpha(0);

    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle(); // defaults first, so re-applying at another DPI scale never compounds

    // --- shape ---
    style.WindowRounding    = m.windowRounding;
    style.ChildRounding     = m.frameRounding + 2.0f;
    style.FrameRounding     = m.frameRounding;
    style.PopupRounding     = m.popupRounding;
    style.GrabRounding      = m.frameRounding;
    style.TabRounding       = m.tabRounding;
    style.ScrollbarRounding = m.scrollbarRounding;
    style.WindowBorderSize  = m.windowBorderSize;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = m.frameBorderSize;
    style.TabBorderSize     = 0.0f;
    style.WindowPadding     = ImVec2(m.windowPaddingX, m.windowPaddingY);
    style.FramePadding      = ImVec2(m.framePaddingX, m.framePaddingY);
    style.ItemSpacing       = ImVec2(m.itemSpacingX, m.itemSpacingY);
    style.ItemInnerSpacing  = ImVec2(8.0f, 6.0f);
    style.ScrollbarSize     = 12.0f;
    style.GrabMinSize       = 14.0f;
    style.ButtonTextAlign   = ImVec2(0.5f, 0.5f);

    // --- colours ---
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                  = toImVec4(p.text);
    c[ImGuiCol_TextDisabled]          = toImVec4(p.textMuted);
    c[ImGuiCol_WindowBg]              = toImVec4(p.background);
    c[ImGuiCol_ChildBg]               = toImVec4(p.surface);
    c[ImGuiCol_PopupBg]               = toImVec4(p.surfaceRaised);
    c[ImGuiCol_Border]                = toImVec4(p.border);
    c[ImGuiCol_BorderShadow]          = toImVec4(transparent);
    c[ImGuiCol_FrameBg]               = toImVec4(p.surfaceRaised);
    c[ImGuiCol_FrameBgHovered]        = toImVec4(p.surfaceRaised.mix(p.accent, 0.22f));
    c[ImGuiCol_FrameBgActive]         = toImVec4(p.surfaceRaised.mix(p.accent, 0.38f));
    c[ImGuiCol_TitleBg]               = toImVec4(p.background);
    c[ImGuiCol_TitleBgActive]         = toImVec4(p.surface);
    c[ImGuiCol_TitleBgCollapsed]      = toImVec4(p.background);
    c[ImGuiCol_MenuBarBg]             = toImVec4(p.surface);
    c[ImGuiCol_ScrollbarBg]           = toImVec4(transparent);
    c[ImGuiCol_ScrollbarGrab]         = toImVec4(p.border.lighten(0.10f));
    c[ImGuiCol_ScrollbarGrabHovered]  = toImVec4(p.accent.withAlpha(200));
    c[ImGuiCol_ScrollbarGrabActive]   = toImVec4(p.accentHover);
    c[ImGuiCol_CheckMark]             = toImVec4(p.accentText);
    c[ImGuiCol_CheckboxSelectedBg]    = toImVec4(p.accent.withAlpha(110));
    c[ImGuiCol_SliderGrab]            = toImVec4(p.accent);
    c[ImGuiCol_SliderGrabActive]      = toImVec4(p.accentHover);
    c[ImGuiCol_Button]                = toImVec4(p.accent);
    c[ImGuiCol_ButtonHovered]         = toImVec4(p.accentHover);
    c[ImGuiCol_ButtonActive]          = toImVec4(p.accentActive);
    c[ImGuiCol_Header]                = toImVec4(p.accent.withAlpha(80));
    c[ImGuiCol_HeaderHovered]         = toImVec4(p.accent.withAlpha(130));
    c[ImGuiCol_HeaderActive]          = toImVec4(p.accent.withAlpha(180));
    c[ImGuiCol_Separator]             = toImVec4(p.border);
    c[ImGuiCol_SeparatorHovered]      = toImVec4(p.accent);
    c[ImGuiCol_SeparatorActive]       = toImVec4(p.accentHover);
    c[ImGuiCol_ResizeGrip]            = toImVec4(p.accent.withAlpha(60));
    c[ImGuiCol_ResizeGripHovered]     = toImVec4(p.accent.withAlpha(150));
    c[ImGuiCol_ResizeGripActive]      = toImVec4(p.accent.withAlpha(230));
    c[ImGuiCol_InputTextCursor]       = toImVec4(p.text);
    c[ImGuiCol_Tab]                   = toImVec4(p.surface);
    c[ImGuiCol_TabHovered]            = toImVec4(p.accent.withAlpha(150));
    c[ImGuiCol_TabSelected]           = toImVec4(p.accent);
    c[ImGuiCol_TabSelectedOverline]   = toImVec4(p.accentText);
    c[ImGuiCol_TabDimmed]             = toImVec4(p.surface);
    c[ImGuiCol_TabDimmedSelected]     = toImVec4(p.accent.mix(p.surface, 0.35f));
    c[ImGuiCol_TabDimmedSelectedOverline] = toImVec4(p.accent);
    c[ImGuiCol_PlotLines]             = toImVec4(p.accentText);
    c[ImGuiCol_PlotLinesHovered]      = toImVec4(p.accentHover);
    c[ImGuiCol_PlotHistogram]         = toImVec4(p.accent);
    c[ImGuiCol_PlotHistogramHovered]  = toImVec4(p.accentHover);
    c[ImGuiCol_TableHeaderBg]         = toImVec4(p.surface);
    c[ImGuiCol_TableBorderStrong]     = toImVec4(p.border);
    c[ImGuiCol_TableBorderLight]      = toImVec4(p.border.darken(0.4f));
    c[ImGuiCol_TableRowBg]            = toImVec4(transparent);
    c[ImGuiCol_TableRowBgAlt]         = toImVec4(p.surface.withAlpha(120));
    c[ImGuiCol_TextLink]              = toImVec4(p.accentText);
    c[ImGuiCol_TextSelectedBg]        = toImVec4(p.accent.withAlpha(100));
    c[ImGuiCol_TreeLines]             = toImVec4(p.border);
    c[ImGuiCol_DragDropTarget]        = toImVec4(p.accentHover);
    c[ImGuiCol_NavCursor]             = toImVec4(p.accentText);
    c[ImGuiCol_NavWindowingHighlight] = toImVec4(Color(255, 255, 255, 180));
    c[ImGuiCol_NavWindowingDimBg]     = toImVec4(Color(0, 0, 0, 120));
    c[ImGuiCol_ModalWindowDimBg]      = toImVec4(Color(0, 0, 0, 170));

    // --- scale for high-DPI screens (positions/sizes scale here, the font through FontScaleDpi) ---
    style.ScaleAllSizes(dpiScale);
    style.FontSizeBase = m.fontSize;
    style.FontScaleDpi = dpiScale;
}

} // namespace ui
