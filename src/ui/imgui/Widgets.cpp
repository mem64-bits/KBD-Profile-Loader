#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "ui/imgui/Widgets.h"
#include "ui/imgui/ImGuiTheme.h"

#include <imgui_internal.h>

#include <cfloat>
#include <cstdio>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
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

bool slider(const char* id, float& value, const float minimum, const float maximum, const float step, const float width,
            const ThemePalette& palette, const SliderMarks& marks) {
    const float em = ImGui::GetFontSize();
    const float height = ImGui::GetFrameHeight();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float knob = em * 0.55f;
    const float left = origin.x + knob;
    const float right = origin.x + std::max(width, knob * 4.0f) - knob;
    const float centreY = origin.y + height * 0.5f;
    const float range = std::max(maximum - minimum, 1e-6f);

    ImGui::PushID(id);
    ImGui::InvisibleButton("##slider", ImVec2(std::max(width, knob * 4.0f), height));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool focused = ImGui::IsItemFocused();
    const float before = value;

    const auto snap = [&](const float v) {
        const float snapped = step > 0.0f ? minimum + std::round((v - minimum) / step) * step : v;
        return std::clamp(snapped, minimum, maximum);
    };
    if (active) {
        const float mouse = ImGui::GetIO().MousePos.x;
        value = snap(minimum + (mouse - left) / (right - left) * range);
    }
    if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
        value = snap(value + (ImGui::GetIO().MouseWheel > 0.0f ? step : -step));
        ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, ImGui::GetItemID());
    }
    if (focused) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow)) value = snap(value - step);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)) value = snap(value + step);
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float trackHalf = em * 0.17f;
    const float x = left + (std::clamp(value, minimum, maximum) - minimum) / range * (right - left);
    // Track, then the filled part as a soft gradient into the accent colour.
    draw->AddRectFilled(ImVec2(left - trackHalf, centreY - trackHalf), ImVec2(right + trackHalf, centreY + trackHalf),
                        toImU32(palette.surfaceRaised.lighten(0.06f)), trackHalf);
    if (marks.fillFromRight) {
        draw->AddRectFilled(ImVec2(x, centreY - trackHalf), ImVec2(right + trackHalf, centreY + trackHalf), toImU32(palette.accent), trackHalf);
    } else {
        draw->AddRectFilledMultiColor(ImVec2(left - trackHalf, centreY - trackHalf), ImVec2(x, centreY + trackHalf),
                                      toImU32(palette.accent.mix(palette.surfaceRaised, 0.45f)), toImU32(palette.accent),
                                      toImU32(palette.accent), toImU32(palette.accent.mix(palette.surfaceRaised, 0.45f)));
        draw->AddCircleFilled(ImVec2(left - trackHalf * 0.2f, centreY), trackHalf, toImU32(palette.accent.mix(palette.surfaceRaised, 0.45f)));
    }
    // Ticks.
    if (marks.every > 0.0f) {
        for (float v = minimum; v <= maximum + 1e-4f; v += marks.every) {
            const float tx = left + (v - minimum) / range * (right - left);
            const bool under = marks.fillFromRight ? tx >= x : tx <= x;
            draw->AddCircleFilled(ImVec2(tx, centreY), trackHalf * 0.45f, toImU32(under ? palette.textOnAccent.withAlpha(150) : palette.textMuted.withAlpha(140)));
        }
    }
    // Extra markers (e.g. where another setting sits).
    for (const float m : marks.markers) {
        const float mx = left + (std::clamp(m, minimum, maximum) - minimum) / range * (right - left);
        draw->AddLine(ImVec2(mx, centreY - em * 0.45f), ImVec2(mx, centreY + em * 0.45f), toImU32(palette.warning), 2.0f);
    }
    // Knob with a soft shadow; it grows a little under the mouse.
    const float r = knob * (active ? 1.08f : hovered ? 1.0f : 0.9f);
    draw->AddCircleFilled(ImVec2(x, centreY + r * 0.18f), r * 1.05f, IM_COL32(0, 0, 0, 70), 32);
    if (hovered || active || focused) draw->AddCircleFilled(ImVec2(x, centreY), r * 1.7f, toImU32(palette.accent.withAlpha(active ? 70 : 40)), 32);
    draw->AddCircleFilled(ImVec2(x, centreY), r, toImU32(palette.text), 32);
    draw->AddCircle(ImVec2(x, centreY), r, toImU32(palette.accent), 32, std::max(2.0f, em * 0.14f));

    // Value bubble while dragging.
    if ((active || hovered) && marks.format != nullptr) {
        char text[32];
        std::snprintf(text, sizeof(text), marks.format, static_cast<double>(value * marks.displayScale));
        const ImVec2 size = ImGui::CalcTextSize(text);
        const ImVec2 pad(em * 0.4f, em * 0.15f);
        const ImVec2 bmin(x - size.x * 0.5f - pad.x, origin.y - size.y - pad.y * 2.0f - em * 0.25f);
        const ImVec2 bmax(x + size.x * 0.5f + pad.x, origin.y - em * 0.25f);
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        fg->AddRectFilled(bmin, bmax, toImU32(palette.accent), em * 0.3f);
        fg->AddTriangleFilled(ImVec2(x - em * 0.25f, bmax.y), ImVec2(x + em * 0.25f, bmax.y), ImVec2(x, bmax.y + em * 0.25f), toImU32(palette.accent));
        fg->AddText(bmin + pad, toImU32(palette.textOnAccent), text);
    }
    if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::PopID();
    return value != before;
}

bool millimetreSlider(const char* id, std::uint8_t& tenths, const std::uint8_t minimum, const std::uint8_t maximum, const float width,
                      const ThemePalette& palette, const std::vector<float>& markers) {
    float value = tenths;
    SliderMarks marks;
    marks.format = "%.1f mm";
    marks.displayScale = 0.1f;
    marks.every = 5.0f;
    marks.markers = markers;
    const bool changed = slider(id, value, minimum, maximum, 1.0f, width, palette, marks);
    if (changed) tenths = static_cast<std::uint8_t>(std::clamp<int>(static_cast<int>(std::lround(value)), minimum, maximum));
    return changed;
}

bool toggle(const char* label, bool& value, const ThemePalette& palette) {
    const float em = ImGui::GetFontSize();
    const float h = em * 1.15f;
    const float w = h * 1.85f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float lineH = ImGui::GetFrameHeight();
    const ImVec2 min(origin.x, origin.y + (lineH - h) * 0.5f);
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    const float total = w + (labelSize.x > 0.0f ? em * 0.6f + labelSize.x : 0.0f);
    ImGui::PushID(label);
    const bool clicked = ImGui::InvisibleButton("##toggle", ImVec2(total, lineH));
    ImGui::PopID();
    if (clicked) value = !value;
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImGuiID id = ImGui::GetItemID();
    // A short slide animation keyed on the item.
    ImGuiStorage* storage = ImGui::GetStateStorage();
    float t = storage->GetFloat(id, value ? 1.0f : 0.0f);
    t += (value ? 1.0f : -1.0f) * ImGui::GetIO().DeltaTime * 8.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    storage->SetFloat(id, t);
    const Color track = palette.surfaceRaised.lighten(0.08f).mix(palette.accent, t);
    draw->AddRectFilled(min, ImVec2(min.x + w, min.y + h), toImU32(hovered ? track.lighten(0.06f) : track), h * 0.5f);
    const float r = h * 0.5f - em * 0.14f;
    const ImVec2 knob(min.x + h * 0.5f + (w - h) * t, min.y + h * 0.5f);
    draw->AddCircleFilled(knob + ImVec2(0, r * 0.12f), r, IM_COL32(0, 0, 0, 60), 24);
    draw->AddCircleFilled(knob, r, toImU32(palette.text), 24);
    if (labelSize.x > 0.0f) {
        draw->AddText(ImVec2(min.x + w + em * 0.6f, origin.y + (lineH - labelSize.y) * 0.5f), toImU32(palette.text), label,
                      ImGui::FindRenderedTextEnd(label));
    }
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return clicked;
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

bool beginCard(const char* id, const ThemePalette& palette) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toImVec4(palette.surface));
    ImGui::PushStyleColor(ImGuiCol_Border, toImVec4(palette.border));
    return ImGui::BeginChild(id, ImVec2(0, 0),
                             ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

void endCard() {
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

void mutedWrapped(const char* text, const ThemePalette& palette) {
    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(palette.textMuted));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void alignRight(const float width) {
    ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width);
}

int segmented(const char* id, const std::span<const char* const> labels, const int selected, const ThemePalette& palette, const float width) {
    int clicked = -1;
    if (labels.empty()) return clicked;
    ImGui::PushID(id);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float em = ImGui::GetFontSize();
    const float pad = style.FramePadding.x * 1.6f;
    const float inset = std::max(2.0f, em * 0.18f);

    // Every item is at least as wide as its label (labels are never cut off); a requested width only adds room.
    float widest = 0.0f;
    float natural = 0.0f;
    for (const char* label : labels) {
        const float w = ImGui::CalcTextSize(label, nullptr, true).x + pad * 2.0f;
        widest = std::max(widest, w);
        natural += w;
    }
    const auto count = static_cast<float>(labels.size());
    const float itemWidth = std::max(widest, (std::max(width, natural) - inset * 2.0f) / count);
    const float height = ImGui::GetFrameHeight() + inset * 2.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 trackMax(origin.x + itemWidth * count + inset * 2.0f, origin.y + height);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float rounding = height * 0.5f;
    draw->AddRectFilled(origin, trackMax, toImU32(palette.surfaceRaised), rounding);
    draw->AddRect(origin, trackMax, toImU32(palette.border.withAlpha(150)), rounding);

    for (std::size_t i = 0; i < labels.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const ImVec2 min(origin.x + inset + itemWidth * static_cast<float>(i), origin.y + inset);
        const ImVec2 max(min.x + itemWidth, min.y + ImGui::GetFrameHeight());
        ImGui::SetCursorScreenPos(min);
        if (ImGui::InvisibleButton("##seg", ImVec2(itemWidth, ImGui::GetFrameHeight()))) clicked = static_cast<int>(i);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = static_cast<int>(i) == selected;
        if (active) {
            draw->AddRectFilled(min, max, toImU32(palette.accent), (max.y - min.y) * 0.5f);
        } else if (hovered) {
            draw->AddRectFilled(min, max, toImU32(palette.text.withAlpha(18)), (max.y - min.y) * 0.5f);
        }
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const char* end = ImGui::FindRenderedTextEnd(labels[i]);
        const ImVec2 text = ImGui::CalcTextSize(labels[i], end);
        draw->AddText(ImVec2(std::floor(min.x + (itemWidth - text.x) * 0.5f), std::floor(min.y + (max.y - min.y - text.y) * 0.5f)),
                      toImU32(active ? palette.textOnAccent : hovered ? palette.text : palette.textMuted), labels[i], end);
        ImGui::PopID();
    }
    // One item covering the whole track, so SameLine / IsItemHovered work on the control as a whole.
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(trackMax.x - origin.x, height));
    ImGui::PopID();
    return clicked;
}

std::optional<std::size_t> slotButtons(const AppSnapshot& state, const std::size_t selected, const ThemePalette& palette) {
    std::optional<std::size_t> clicked;
    const float spacing   = ImGui::GetStyle().ItemSpacing.x;
    const float slotWidth = (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f;
    for (std::size_t i = 0; i < SLOT_COUNT; ++i) {
        if (i > 0) ImGui::SameLine();
        ImGui::PushID(static_cast<int>(i));
        std::string label = slotLabel(i);
        if (state.slots[i].hasProfile) label += ": " + state.slots[i].name;
        if (state.activeSlot == static_cast<int>(i)) label += "  *";
        const bool pressed = (i == selected) ? primaryButton(label.c_str(), ImVec2(slotWidth, 0), palette)
                                             : secondaryButton(label.c_str(), ImVec2(slotWidth, 0), palette);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", state.activeSlot == static_cast<int>(i) ? "Active on the keyboard" : "Edit this slot");
        if (pressed) clicked = i;
        ImGui::PopID();
    }
    return clicked;
}

LayoutKind layoutFor(const AppSnapshot& state, const std::optional<LayoutKind> picked) noexcept {
    if (picked) return *picked;
    return state.device.variant == kbd::KeyboardVariant::Ansi ? LayoutKind::Ansi : LayoutKind::Iso;
}

const char* keyName(const KeyGeometry& key) noexcept {
    if (key.legend[0] != '\0') return key.legend;
    switch (key.glyph) {
        case KeyGlyph::ArrowUp:    return "Up";
        case KeyGlyph::ArrowDown:  return "Down";
        case KeyGlyph::ArrowLeft:  return "Left";
        case KeyGlyph::ArrowRight: return "Right";
        case KeyGlyph::None:       break;
    }
    return "Space";
}

std::string keyName(const KeyPosition& key, const LayoutKind layout) {
    if (const KeyGeometry* geometry = keyboardLayout(layout).find(key)) return keyName(*geometry);
    return "R" + std::to_string(key.row) + "C" + std::to_string(key.col);
}

void drawIcon(ImDrawList* draw, const Icon icon, const ImVec2 c, const float size, const ImU32 color, const float thickness) {
    const float s = size * 0.5f;
    switch (icon) {
        case Icon::Profiles: { // three stacked layers
            for (int i = 0; i < 3; ++i) {
                const float y = c.y - s * 0.55f + static_cast<float>(i) * s * 0.55f;
                const std::array<ImVec2, 4> layer{ImVec2(c.x - s, y), ImVec2(c.x, y - s * 0.38f), ImVec2(c.x + s, y), ImVec2(c.x, y + s * 0.38f)};
                if (i == 0) {
                    draw->AddPolyline(layer.data(), 4, color, ImDrawFlags_Closed, thickness);
                } else {
                    draw->AddLine(layer[0], layer[3], color, thickness);
                    draw->AddLine(layer[3], layer[2], color, thickness);
                }
            }
            break;
        }
        case Icon::Actuation: { // a key switch: stem over a travel line with a marker
            draw->AddRect(ImVec2(c.x - s * 0.75f, c.y - s * 0.9f), ImVec2(c.x + s * 0.75f, c.y + s * 0.1f), color, s * 0.2f, 0, thickness);
            draw->AddLine(ImVec2(c.x, c.y + s * 0.1f), ImVec2(c.x, c.y + s * 0.55f), color, thickness);
            draw->AddLine(ImVec2(c.x - s, c.y + s * 0.85f), ImVec2(c.x + s, c.y + s * 0.85f), color, thickness);
            draw->AddCircleFilled(ImVec2(c.x + s * 0.35f, c.y + s * 0.85f), thickness * 1.4f, color);
            break;
        }
        case Icon::Movement: { // four arrows out of a centre (WASD)
            const float a = s * 0.95f;
            const float head = s * 0.32f;
            const std::array<ImVec2, 4> dirs{ImVec2(0, -1), ImVec2(1, 0), ImVec2(0, 1), ImVec2(-1, 0)};
            for (const ImVec2& d : dirs) {
                const ImVec2 tip(c.x + d.x * a, c.y + d.y * a);
                draw->AddLine(ImVec2(c.x + d.x * s * 0.25f, c.y + d.y * s * 0.25f), tip, color, thickness);
                const ImVec2 n(-d.y, d.x);
                draw->AddLine(tip, ImVec2(tip.x - d.x * head + n.x * head, tip.y - d.y * head + n.y * head), color, thickness);
                draw->AddLine(tip, ImVec2(tip.x - d.x * head - n.x * head, tip.y - d.y * head - n.y * head), color, thickness);
            }
            break;
        }
        case Icon::Gamepad: { // controller outline with a cross and two buttons
            draw->AddRect(ImVec2(c.x - s, c.y - s * 0.55f), ImVec2(c.x + s, c.y + s * 0.55f), color, s * 0.55f, 0, thickness);
            const ImVec2 d(c.x - s * 0.45f, c.y);
            draw->AddLine(ImVec2(d.x - s * 0.25f, d.y), ImVec2(d.x + s * 0.25f, d.y), color, thickness);
            draw->AddLine(ImVec2(d.x, d.y - s * 0.25f), ImVec2(d.x, d.y + s * 0.25f), color, thickness);
            draw->AddCircleFilled(ImVec2(c.x + s * 0.35f, c.y + s * 0.12f), thickness * 1.3f, color);
            draw->AddCircleFilled(ImVec2(c.x + s * 0.6f, c.y - s * 0.12f), thickness * 1.3f, color);
            break;
        }
        case Icon::Settings: { // gear: ring with eight teeth
            draw->AddCircle(c, s * 0.42f, color, 0, thickness);
            for (int i = 0; i < 8; ++i) {
                const float angle = static_cast<float>(i) * std::numbers::pi_v<float> / 4.0f;
                const ImVec2 dir(std::cos(angle), std::sin(angle));
                draw->AddLine(ImVec2(c.x + dir.x * s * 0.62f, c.y + dir.y * s * 0.62f), ImVec2(c.x + dir.x * s * 0.95f, c.y + dir.y * s * 0.95f),
                              color, thickness * 1.6f);
            }
            draw->AddCircle(c, s * 0.68f, color, 0, thickness);
            break;
        }
    }
}

void drawFaceSymbol(ImDrawList* draw, const gamepad::Button button, const gamepad::LabelStyle style, const ImVec2 c, const float r,
                    const ImU32 color) {
    using gamepad::Button;
    const float t = std::max(1.2f, r * 0.22f);
    if (style == gamepad::LabelStyle::PlayStation) {
        const float s = r * 0.55f;
        switch (button) {
            case Button::South: // cross
                draw->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), color, t);
                draw->AddLine(ImVec2(c.x - s, c.y + s), ImVec2(c.x + s, c.y - s), color, t);
                return;
            case Button::East: // circle
                draw->AddCircle(c, s, color, 0, t);
                return;
            case Button::West: // square
                draw->AddRect(ImVec2(c.x - s * 0.85f, c.y - s * 0.85f), ImVec2(c.x + s * 0.85f, c.y + s * 0.85f), color, 0.0f, 0, t);
                return;
            case Button::North: // triangle
                draw->AddTriangle(ImVec2(c.x, c.y - s), ImVec2(c.x + s, c.y + s * 0.75f), ImVec2(c.x - s, c.y + s * 0.75f), color, t);
                return;
            default:
                break;
        }
    }
    const char* letter = gamepad::buttonName(button, gamepad::LabelStyle::Xbox);
    ImFont* font = ImGui::GetFont();
    const float size = r * 1.25f;
    const ImVec2 textSize = font->CalcTextSizeA(size, FLT_MAX, 0.0f, letter);
    draw->AddText(font, size, ImVec2(c.x - textSize.x * 0.5f, c.y - textSize.y * 0.5f), color, letter);
}

void drawFaceButton(ImDrawList* draw, const gamepad::Button button, const gamepad::LabelStyle style, const ImVec2 c, const float r,
                    const ImU32 fill, const ImU32 symbol, const ImU32 outline) {
    draw->AddCircleFilled(c, r, fill);
    if (outline != 0) draw->AddCircle(c, r, outline, 0, std::max(1.0f, r * 0.08f));
    drawFaceSymbol(draw, button, style, c, r, symbol);
}

void drawDirection(ImDrawList* draw, const int numpad, const ImVec2 c, const float r, const ImU32 color) {
    if (numpad == 5 || numpad < 1 || numpad > 9) {
        draw->AddCircleFilled(c, r * 0.22f, color);
        return;
    }
    const int dx = (numpad - 1) % 3 - 1;
    const int dy = (numpad - 1) / 3 - 1; // +1 = up
    float x = static_cast<float>(dx);
    float y = -static_cast<float>(dy);    // screen y grows downwards
    const float length = std::sqrt(x * x + y * y);
    x /= length;
    y /= length;
    const ImVec2 tip(c.x + x * r, c.y + y * r);
    const ImVec2 tail(c.x - x * r * 0.75f, c.y - y * r * 0.75f);
    const ImVec2 normal(-y, x);
    const float head = r * 0.62f;
    const ImVec2 base(tip.x - x * head, tip.y - y * head);
    draw->AddLine(tail, base, color, std::max(1.5f, r * 0.26f));
    draw->AddTriangleFilled(tip, ImVec2(base.x + normal.x * head * 0.62f, base.y + normal.y * head * 0.62f),
                            ImVec2(base.x - normal.x * head * 0.62f, base.y - normal.y * head * 0.62f), color);
}

} // namespace ui::widgets
