#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "ui/imgui/Widgets.h"
#include "ui/imgui/ImGuiTheme.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace ui::widgets {

float pillHeight() {
    return ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y * 0.9f;
}

float buttonWidth(const char* label) {
    return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

ImVec2 pill(const char* text, const Color& fill, const Color& textColor, const std::optional<Color>& dot) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const ImVec2 padding(style.FramePadding.x * 0.8f, style.FramePadding.y * 0.45f);
    const float  dotSpace = dot ? textSize.y * 0.45f + padding.x * 0.6f : 0.0f;

    const ImVec2 size(textSize.x + padding.x * 2.0f + dotSpace, textSize.y + padding.y * 2.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(origin, origin + size, toImU32(fill), size.y * 0.5f);
    if (dot) {
        const float radius = textSize.y * 0.22f;
        draw->AddCircleFilled(ImVec2(origin.x + padding.x + radius, origin.y + size.y * 0.5f), radius, toImU32(*dot));
    }
    draw->AddText(ImVec2(origin.x + padding.x + dotSpace, origin.y + padding.y), toImU32(textColor), text);
    ImGui::Dummy(size);
    return size;
}

void labelWithValue(const char* label, const std::string& value, const ThemePalette& palette) {
    sectionLabel(label, palette);
    ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(value.c_str()).x);
    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(palette.accentText));
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopStyleColor();
}

void sectionLabel(const char* text, const ThemePalette& palette) {
    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(palette.textMuted));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

bool primaryButton(const char* label, const ImVec2& size, const ThemePalette& palette) {
    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(palette.textOnAccent));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor();
    return pressed;
}

bool secondaryButton(const char* label, const ImVec2& size, const ThemePalette& palette) {
    ImGui::PushStyleColor(ImGuiCol_Button, toImVec4(palette.surfaceRaised));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, toImVec4(palette.surfaceRaised.mix(palette.accent, 0.35f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, toImVec4(palette.surfaceRaised.mix(palette.accent, 0.55f)));
    ImGui::PushStyleColor(ImGuiCol_Border, toImVec4(palette.border.lighten(0.12f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return pressed;
}

bool millimetreSlider(const char* id, std::uint8_t& tenths, const std::uint8_t minimum, const std::uint8_t maximum, const float width) {
    int value = tenths;
    ImGui::SetNextItemWidth(width);
    const bool changed = ImGui::SliderInt(id, &value, minimum, maximum, "", ImGuiSliderFlags_AlwaysClamp);
    if (changed) tenths = static_cast<std::uint8_t>(std::clamp<int>(value, minimum, maximum));
    return changed;
}

void travelDiagram(const float width, const std::uint8_t actuationTenths, const ThemePalette& palette) {
    const float fontSize = ImGui::GetFontSize();
    const float barHeight = fontSize * 1.1f;
    const float rounding = barHeight * 0.3f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const float total = static_cast<float>(actuation::TOTAL_TRAVEL_TENTHS);
    const auto xAt = [&](const float tenths) { return origin.x + width * (tenths / total); };
    const float bottom = origin.y + barHeight;

    // Track, then the part of the travel that is "pressed" when the key actuates.
    draw->AddRectFilled(origin, ImVec2(origin.x + width, bottom), toImU32(palette.surfaceRaised), rounding);
    const float actuationX = xAt(static_cast<float>(actuationTenths));
    draw->AddRectFilled(origin, ImVec2(actuationX, bottom), toImU32(palette.accent.withAlpha(230)), rounding, ImDrawFlags_RoundCornersLeft);
    draw->AddRect(origin, ImVec2(origin.x + width, bottom), toImU32(palette.border), rounding);

    // Actuation marker.
    draw->AddLine(ImVec2(actuationX, origin.y - fontSize * 0.2f), ImVec2(actuationX, bottom + fontSize * 0.2f), toImU32(palette.accentText), 2.0f);
    draw->AddCircleFilled(ImVec2(actuationX, origin.y - fontSize * 0.2f), fontSize * 0.2f, toImU32(palette.accentText));

    // Millimetre scale below the bar.
    for (int mm = 0; mm <= 4; ++mm) {
        const float x = xAt(static_cast<float>(mm * 10));
        draw->AddLine(ImVec2(x, bottom), ImVec2(x, bottom + fontSize * 0.3f), toImU32(palette.border.lighten(0.2f)));

        const std::string label = mm == 4 ? "4 mm" : std::to_string(mm);
        const ImVec2 size = ImGui::CalcTextSize(label.c_str());
        const float labelX = std::clamp(x - size.x * 0.5f, origin.x, origin.x + width - size.x);
        draw->AddText(ImVec2(labelX, bottom + fontSize * 0.38f), toImU32(palette.textMuted), label.c_str());
    }
    ImGui::Dummy(ImVec2(width, barHeight + fontSize * 1.5f));
}

void spinner(const float radius, const Color& color) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 center(origin.x + radius, origin.y + ImGui::GetTextLineHeight() * 0.5f);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const float start = static_cast<float>(ImGui::GetTime()) * 6.0f;
    draw->PathClear();
    draw->PathArcTo(center, radius, start, start + 4.2f);
    draw->PathStroke(toImU32(color), ImDrawFlags_None, std::max(2.0f, radius * 0.28f));
    ImGui::Dummy(ImVec2(radius * 2.0f, ImGui::GetTextLineHeight()));
}

} // namespace ui::widgets
