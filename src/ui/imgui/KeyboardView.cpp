#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "ui/imgui/KeyboardView.h"
#include "ui/imgui/Glyphs.h"
#include "ui/imgui/ImGuiTheme.h"
#include "ui/imgui/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace ui {

namespace {

    constexpr float BOARD_PADDING_U   = 0.30f;  // chassis margin around the keys, in key units
    constexpr float KEY_GAP_U         = 0.045f; // half of the gap between two neighbouring keycaps
    constexpr float DRAG_THRESHOLD_PX = 4.0f;

    struct Box {
        ImVec2 min;
        ImVec2 max;

        [[nodiscard]] bool contains(const ImVec2& p) const noexcept {
            return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
        }
        [[nodiscard]] bool overlaps(const Box& other) const noexcept {
            return min.x <= other.max.x && max.x >= other.min.x && min.y <= other.max.y && max.y >= other.min.y;
        }
    };

    // A keycap on screen: one rectangle, or two for a stepped key (ISO Enter: `extra` is its wider top part).
    struct KeyShape {
        Box  main;
        Box  extra;
        bool stepped{false};

        [[nodiscard]] bool contains(const ImVec2& p) const noexcept { return main.contains(p) || (stepped && extra.contains(p)); }
        [[nodiscard]] bool overlaps(const Box& b) const noexcept { return main.overlaps(b) || (stepped && extra.overlaps(b)); }
    };

    KeyShape shapeOf(const KeyGeometry& key, const ImVec2 origin, const float unit) {
        const float gap = KEY_GAP_U * unit;
        const auto box = [&](const float x, const float y, const float w, const float h) {
            return Box{ImVec2(std::floor(origin.x + x * unit + gap), std::floor(origin.y + y * unit + gap)),
                       ImVec2(std::floor(origin.x + (x + w) * unit - gap), std::floor(origin.y + (y + h) * unit - gap))};
        };
        KeyShape shape{box(key.x, key.y, key.w, key.h), {}, key.isStepped()};
        if (shape.stepped) shape.extra = box(key.x + key.x2, key.y + key.y2, key.w2, key.h2);
        return shape;
    }

    // Outline of an ISO Enter (wide top part, narrower body), shrunk by the given insets.
    std::array<ImVec2, 6> steppedOutline(const KeyShape& s, const float side, const float top, const float bottom) {
        return {ImVec2(s.extra.min.x + side, s.extra.min.y + top),
                ImVec2(s.main.max.x - side, s.extra.min.y + top),
                ImVec2(s.main.max.x - side, s.main.max.y - bottom),
                ImVec2(s.main.min.x + side, s.main.max.y - bottom),
                ImVec2(s.main.min.x + side, s.extra.max.y - bottom),
                ImVec2(s.extra.min.x + side, s.extra.max.y - bottom)};
    }

    // The area of the keycap's top face where legends go.
    Box topFace(const KeyShape& s, const float side, const float top, const float bottom) {
        if (!s.stepped) return Box{s.main.min + ImVec2(side, top), s.main.max - ImVec2(side, bottom)};
        return Box{ImVec2(s.extra.min.x + side, s.extra.min.y + top), ImVec2(s.main.max.x - side, s.main.max.y - bottom)};
    }

    Color keycapColor(const KeyRole role, const ThemePalette& p) {
        switch (role) {
            case KeyRole::Accent:   return p.warning.mix(p.surfaceRaised, 0.80f); // warm, so it never reads as "sensitive"
            case KeyRole::Modifier: return p.surfaceRaised.mix(p.background, 0.35f);
            case KeyRole::Alpha:
            default:                return p.surfaceRaised.lighten(0.07f);
        }
    }

    Color readableOn(const Color& fill, const ThemePalette& p) {
        return Color::contrastRatio(fill, p.text) >= Color::contrastRatio(fill, p.background) ? p.text : p.background;
    }

    ImVec2 textSize(ImFont* font, const float size, const char* text) {
        return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    }

    // Largest size <= `size` at which `text` fits into `room` pixels.
    float fitSize(ImFont* font, const float size, const char* text, const float room) {
        const float width = textSize(font, size, text).x;
        if (width <= room || width <= 0.0f) return size;
        return std::max(size * room / width, size * 0.55f);
    }

    void drawArrow(ImDrawList* draw, const KeyGlyph glyph, const ImVec2 centre, const float r, const ImU32 color) {
        switch (glyph) {
            case KeyGlyph::ArrowUp:
                draw->AddTriangleFilled(centre + ImVec2(0, -r), centre + ImVec2(r, r * 0.7f), centre + ImVec2(-r, r * 0.7f), color);
                break;
            case KeyGlyph::ArrowDown:
                draw->AddTriangleFilled(centre + ImVec2(-r, -r * 0.7f), centre + ImVec2(r, -r * 0.7f), centre + ImVec2(0, r), color);
                break;
            case KeyGlyph::ArrowLeft:
                draw->AddTriangleFilled(centre + ImVec2(-r, 0), centre + ImVec2(r * 0.7f, -r), centre + ImVec2(r * 0.7f, r), color);
                break;
            case KeyGlyph::ArrowRight:
                draw->AddTriangleFilled(centre + ImVec2(-r * 0.7f, -r), centre + ImVec2(r, 0), centre + ImVec2(-r * 0.7f, r), color);
                break;
            case KeyGlyph::None:
                break;
        }
    }

    struct KeyLook {
        bool selected{false};
        bool hovered{false};
    };

    void drawKey(ImDrawList* draw, const KeyGeometry& key, const KeyShape& shape, const KeyDisplay& shown,
                 const KeyLook& look, const ThemePalette& p, const float unit) {
        ImFont* font = ImGui::GetFont();
        const float rounding = unit * 0.09f;
        const float side     = unit * 0.075f; // keycap wall seen from above
        const float top      = unit * 0.035f;
        const float bottom   = unit * 0.12f;

        // Colours: the keycap's family, tinted by how shallow it actuates (firmware default 2.0 mm = no tint) and
        // darkened a little below the default. Keys with their own settings get the full tint so they stand out
        // from the ones that follow the slot.
        const std::uint8_t act = shown.effective.actuationTenths;
        const float strength = shown.ownSettings ? 1.0f : 0.45f;
        Color face = keycapColor(key.role, p);
        if (shown.tint) {
            face = face.mix(*shown.tint, 0.58f);
        } else if (shown.badge || shown.hideValue) {
            // Pages that are not about actuation leave the keycaps neutral.
        } else if (act < actuation::DEFAULT_ACTUATION_TENTHS) {
            const float shallow = static_cast<float>(actuation::DEFAULT_ACTUATION_TENTHS - act) /
                                  static_cast<float>(actuation::DEFAULT_ACTUATION_TENTHS - actuation::MIN_TENTHS);
            face = face.mix(KeyboardView::heatColor(act, p), (0.22f + 0.43f * shallow) * strength);
        } else if (act > actuation::DEFAULT_ACTUATION_TENTHS) {
            const float deep = static_cast<float>(act - actuation::DEFAULT_ACTUATION_TENTHS) /
                               static_cast<float>(actuation::MAX_TENTHS - actuation::DEFAULT_ACTUATION_TENTHS);
            face = face.mix(p.background, 0.35f * deep * strength);
        }
        if (shown.dim) face = face.mix(p.background, 0.45f);
        if (look.hovered) face = face.lighten(0.08f);
        const Color skirt  = face.darken(0.38f);
        const Color border = look.selected ? p.accentText : face.darken(0.62f);

        // Drop shadow, body (skirt), top face.
        const ImVec2 drop(0.0f, std::max(1.0f, unit * 0.035f));
        if (!shape.stepped) {
            draw->AddRectFilled(shape.main.min + drop, shape.main.max + drop, IM_COL32(0, 0, 0, 110), rounding);
            draw->AddRectFilled(shape.main.min, shape.main.max, toImU32(skirt), rounding);
            const Box f = topFace(shape, side, top, bottom);
            draw->AddRectFilled(f.min, f.max, toImU32(face), rounding * 0.8f);
            draw->AddLine(ImVec2(f.min.x + rounding, f.min.y + 1.0f), ImVec2(f.max.x - rounding, f.min.y + 1.0f), IM_COL32(255, 255, 255, 22));
        } else {
            auto outline = steppedOutline(shape, 0.0f, 0.0f, 0.0f);
            std::array<ImVec2, 6> shadow = outline;
            for (auto& point : shadow) point += drop;
            draw->AddConcavePolyFilled(shadow.data(), static_cast<int>(shadow.size()), IM_COL32(0, 0, 0, 110));
            draw->AddConcavePolyFilled(outline.data(), static_cast<int>(outline.size()), toImU32(skirt));
            auto inner = steppedOutline(shape, side, top, bottom);
            draw->AddConcavePolyFilled(inner.data(), static_cast<int>(inner.size()), toImU32(face));
        }

        // Selection: accent outline plus a soft outer ring.
        if (look.selected) {
            const float t = std::max(1.5f, unit * 0.045f);
            if (!shape.stepped) {
                draw->AddRect(shape.main.min - ImVec2(t, t), shape.main.max + ImVec2(t, t), toImU32(p.accent.withAlpha(110)), rounding + t, 0, t);
                draw->AddRect(shape.main.min, shape.main.max, toImU32(border), rounding, 0, t);
            } else {
                auto ring = steppedOutline(shape, 0.0f, 0.0f, 0.0f);
                draw->AddPolyline(ring.data(), static_cast<int>(ring.size()), toImU32(border), ImDrawFlags_Closed, t);
            }
        } else if (!shape.stepped) {
            draw->AddRect(shape.main.min, shape.main.max, toImU32(border), rounding, 0, 1.0f);
        } else {
            auto ring = steppedOutline(shape, 0.0f, 0.0f, 0.0f);
            draw->AddPolyline(ring.data(), static_cast<int>(ring.size()), toImU32(border), ImDrawFlags_Closed, 1.0f);
        }

        // Legends: shift legend on top, main legend below it (or alone in the top-left corner).
        const Box f   = topFace(shape, side, top, bottom);
        const float pad      = unit * 0.07f;
        const float room     = (f.max.x - f.min.x) - pad * 2.0f;
        const float legendPx = std::clamp(unit * 0.25f, 8.0f, 18.0f);
        const Color legendColor = readableOn(face, p);
        ImVec2 cursor = f.min + ImVec2(pad, pad * 0.6f);

        if (key.shiftLegend[0] != '\0') {
            const float size = legendPx * 0.85f;
            draw->AddText(font, size, cursor, toImU32(legendColor.mix(face, 0.35f)), key.shiftLegend);
            cursor.y += size * 0.95f;
        }
        if (key.legend[0] != '\0') {
            const float size = fitSize(font, legendPx, key.legend, room);
            draw->AddText(font, size, cursor, toImU32(legendColor), key.legend);
        }
        if (key.glyph != KeyGlyph::None) {
            const float r = unit * 0.11f;
            drawArrow(draw, key.glyph, ImVec2((f.min.x + f.max.x) * 0.5f, f.min.y + r + pad), r, toImU32(legendColor));
        }

        // Live travel: the keycap face fills from the top as the key goes down.
        if (shown.level >= 0.0f) {
            const Box fill = topFace(shape, side, top, bottom);
            const float depth = std::clamp(shown.level, 0.0f, 1.0f) * (fill.max.y - fill.min.y);
            if (depth > 0.5f) {
                draw->AddRectFilled(fill.min, ImVec2(fill.max.x, fill.min.y + depth), toImU32(p.accent.withAlpha(110)), rounding * 0.8f);
            }
            draw->AddLine(ImVec2(fill.min.x + 1.0f, fill.min.y + depth), ImVec2(fill.max.x - 1.0f, fill.min.y + depth),
                          toImU32(p.accentText), std::max(1.5f, unit * 0.03f));
        }

        if (shown.badge && shown.badge->prompt && glyphs::hasGlyph(*shown.badge->prompt)) {
            // A controller prompt glyph in the bottom-right corner, with a dark disc so it reads on any tint.
            const float size = std::clamp(unit * 0.5f, 14.0f, 34.0f);
            const bool centred = key.legend[0] == '\0' && key.glyph == KeyGlyph::None;
            const ImVec2 c = centred ? ImVec2((f.min.x + f.max.x) * 0.5f, f.max.y - pad * 0.5f - size * 0.5f)
                                     : ImVec2(f.max.x - pad * 0.5f - size * 0.5f, f.max.y - pad * 0.5f - size * 0.5f);
            draw->AddCircleFilled(c, size * 0.5f, toImU32(p.background.withAlpha(170)), 24);
            if (glyphs::drawTarget(draw, *shown.badge->prompt, shown.badge->style, c, size, toImU32(shown.badge->fill),
                                   shown.badge->zacksly ? glyphs::Pack::Zacksly : glyphs::Pack::Kenney)) return;
        }
        if (shown.badge) {
            const KeyBadge& badge = *shown.badge;
            const float chipPx = std::clamp(unit * 0.23f, 7.0f, 15.0f);
            const bool symbolOnly = badge.faceButton.has_value();
            const ImVec2 textSz = symbolOnly ? ImVec2(chipPx * 0.9f, chipPx) : textSize(font, chipPx, badge.text.c_str());
            const float glyphW = badge.glyph != KeyGlyph::None ? chipPx * 0.8f : 0.0f;
            const ImVec2 chipPad(chipPx * 0.34f, chipPx * 0.1f);
            const ImVec2 chipSize(textSz.x + glyphW + chipPad.x * 2.0f, textSz.y + chipPad.y * 2.0f);
            const bool centred = key.legend[0] == '\0' && key.glyph == KeyGlyph::None;
            const ImVec2 chipMin = centred ? ImVec2((f.min.x + f.max.x - chipSize.x) * 0.5f, f.max.y - pad * 0.6f - chipSize.y)
                                           : ImVec2(f.max.x - pad * 0.7f - chipSize.x, f.max.y - pad * 0.6f - chipSize.y);
            draw->AddRectFilled(chipMin, chipMin + chipSize, toImU32(badge.fill), chipSize.y * 0.45f);
            if (symbolOnly) {
                widgets::drawFaceSymbol(draw, *badge.faceButton, badge.style,
                                        ImVec2(chipMin.x + chipPad.x + textSz.x * 0.5f, chipMin.y + chipSize.y * 0.5f), chipPx * 0.62f,
                                        toImU32(badge.textColor));
            } else {
                draw->AddText(font, chipPx, chipMin + chipPad, toImU32(badge.textColor), badge.text.c_str());
            }
            if (badge.glyph != KeyGlyph::None) {
                drawArrow(draw, badge.glyph, ImVec2(chipMin.x + chipPad.x + textSz.x + glyphW * 0.55f, chipMin.y + chipSize.y * 0.5f),
                          chipPx * 0.3f, toImU32(badge.textColor));
            }
            return;
        }

        if (shown.hideValue) return;

        // Actuation chip at the bottom: filled = the key's own value, outlined = follows the slot.
        char value[8];
        std::snprintf(value, sizeof(value), "%.1f", static_cast<double>(actuation::toMillimetres(act)));
        const float chipPx = std::clamp(unit * 0.22f, 7.0f, 15.0f);
        const ImVec2 valueSize = textSize(font, chipPx, value);
        const ImVec2 chipPad(chipPx * 0.32f, chipPx * 0.08f);
        const ImVec2 chipSize = valueSize + chipPad * 2.0f;
        const bool centred = key.legend[0] == '\0' && key.glyph == KeyGlyph::None; // space bar
        const ImVec2 chipMin = centred ? ImVec2((f.min.x + f.max.x - chipSize.x) * 0.5f, f.max.y - pad * 0.6f - chipSize.y)
                                       : ImVec2(f.max.x - pad * 0.7f - chipSize.x, f.max.y - pad * 0.6f - chipSize.y);
        // Rapid Trigger: a small dot in the bottom-left corner, level with the chip (legends never reach it).
        if (shown.effective.rapidTrigger) {
            const float r = std::max(2.0f, unit * 0.05f);
            const float left = shape.stepped ? shape.main.min.x + side : f.min.x; // ISO Enter: its narrower lower part
            draw->AddCircleFilled(ImVec2(left + pad + r, chipMin.y + chipSize.y * 0.5f), r, toImU32(p.success));
        }

        const Color heat = KeyboardView::heatColor(act, p);
        if (shown.ownSettings) {
            draw->AddRectFilled(chipMin, chipMin + chipSize, toImU32(heat), chipSize.y * 0.5f);
            draw->AddText(font, chipPx, chipMin + chipPad, toImU32(readableOn(heat, p)), value);
        } else {
            draw->AddRect(chipMin, chipMin + chipSize, toImU32(heat.withAlpha(170)), chipSize.y * 0.5f, 0, 1.0f);
            draw->AddText(font, chipPx, chipMin + chipPad, toImU32(legendColor.mix(face, 0.30f)), value);
        }
    }

} // namespace

Color KeyboardView::heatColor(const std::uint8_t actuationTenths, const ThemePalette& palette) noexcept {
    const float t = std::clamp(static_cast<float>(actuationTenths - actuation::MIN_TENTHS) /
                                   static_cast<float>(actuation::MAX_TENTHS - actuation::MIN_TENTHS), 0.0f, 1.0f);
    const Color shallow = palette.accent.lighten(0.22f);
    const Color middle  = palette.accent.mix(palette.textMuted, 0.65f);
    const Color deep    = palette.surfaceRaised.lighten(0.18f);
    return t < 0.5f ? shallow.mix(middle, t * 2.0f) : middle.mix(deep, (t - 0.5f) * 2.0f);
}

void KeyboardView::drawHeatLegend(const ThemePalette& palette, const float width) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const char* left  = "0.1 mm  sensitive";
    const char* right = "deep  3.9 mm";
    const float labelWidth = ImGui::CalcTextSize(left).x;
    const float rightWidth = ImGui::CalcTextSize(right).x;
    const float spacing    = ImGui::GetStyle().ItemSpacing.x;
    const float barWidth   = std::max(40.0f, width - labelWidth - rightWidth - spacing * 2.0f);
    const float lineHeight = ImGui::GetTextLineHeight();

    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(palette.textMuted));
    ImGui::TextUnformatted(left);
    ImGui::SameLine();

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float barHeight = lineHeight * 0.45f;
    const ImVec2 barMin(origin.x, origin.y + (lineHeight - barHeight) * 0.5f);
    constexpr int SEGMENTS = 38;
    for (int i = 0; i < SEGMENTS; ++i) {
        const float x0 = barMin.x + barWidth * static_cast<float>(i) / SEGMENTS;
        const float x1 = barMin.x + barWidth * static_cast<float>(i + 1) / SEGMENTS;
        const auto tenths = static_cast<std::uint8_t>(actuation::MIN_TENTHS + i);
        draw->AddRectFilled(ImVec2(x0, barMin.y), ImVec2(x1 + 0.5f, barMin.y + barHeight), toImU32(heatColor(tenths, palette)));
    }
    // Firmware default (2.0 mm) tick.
    const float tick = barMin.x + barWidth * static_cast<float>(actuation::DEFAULT_ACTUATION_TENTHS - actuation::MIN_TENTHS) /
                                      static_cast<float>(actuation::MAX_TENTHS - actuation::MIN_TENTHS);
    draw->AddLine(ImVec2(tick, barMin.y - 2.0f), ImVec2(tick, barMin.y + barHeight + 2.0f), toImU32(palette.text), 1.5f);
    ImGui::Dummy(ImVec2(barWidth, lineHeight));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The firmware default is 2.0 mm (the white tick).");
    ImGui::SameLine();
    ImGui::TextUnformatted(right);
    ImGui::PopStyleColor();
}

KeyboardView::Result KeyboardView::draw(const char* id, const KeyboardLayout& layout, const DisplayFn& display,
                                        std::set<KeyPosition>& selection, const ThemePalette& p, const float width) {
    Result result;
    const ImGuiIO& io = ImGui::GetIO();

    const float unit = std::floor(width / (layout.width + BOARD_PADDING_U * 2.0f));
    const ImVec2 size(width, std::ceil(unit * (layout.height + BOARD_PADDING_U * 2.0f)));
    const ImVec2 boardMin = ImGui::GetCursorScreenPos();
    const ImVec2 boardMax = boardMin + size;
    const ImVec2 origin   = boardMin + ImVec2(std::floor((width - unit * layout.width) * 0.5f), unit * BOARD_PADDING_U);

    ImGui::InvisibleButton(id, size);
    const bool boardHovered = ImGui::IsItemHovered();
    const bool boardActive  = ImGui::IsItemActive();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Case.
    const float caseRounding = unit * 0.28f;
    draw->AddRectFilled(boardMin + ImVec2(0, unit * 0.06f), boardMax + ImVec2(0, unit * 0.06f), IM_COL32(0, 0, 0, 120), caseRounding);
    draw->AddRectFilled(boardMin, boardMax, toImU32(p.surface.mix(p.background, 0.55f)), caseRounding);
    draw->AddRect(boardMin, boardMax, toImU32(p.border), caseRounding, 0, 1.0f);
    draw->AddLine(boardMin + ImVec2(caseRounding, 1.0f), ImVec2(boardMax.x - caseRounding, boardMin.y + 1.0f), IM_COL32(255, 255, 255, 18));

    std::vector<KeyShape> shapes;
    shapes.reserve(layout.keys.size());
    for (const KeyGeometry& key : layout.keys) shapes.push_back(shapeOf(key, origin, unit));

    int hoveredIndex = -1;
    if (boardHovered || boardActive) {
        for (std::size_t i = 0; i < shapes.size(); ++i) {
            if (shapes[i].contains(io.MousePos)) {
                hoveredIndex = static_cast<int>(i);
                break;
            }
        }
    }
    if (hoveredIndex >= 0) result.hovered = layout.keys[static_cast<std::size_t>(hoveredIndex)].position();

    // --- input ---
    if (ImGui::IsItemActivated()) {
        m_pressedOnBoard = true;
        m_dragging       = false;
        m_dragStart      = io.MousePos;

        if (io.MouseClickedCount[ImGuiMouseButton_Left] == 2 && hoveredIndex >= 0) {
            // Double-click: every key with the same settings as this one.
            const KeyDisplay reference = display(*result.hovered);
            selection.clear();
            for (const KeyGeometry& key : layout.keys) {
                const KeyDisplay other = display(key.position());
                const bool same = (reference.badge || other.badge)
                                      ? (other.badge == reference.badge)
                                      : (other.effective == reference.effective && other.ownSettings == reference.ownSettings);
                if (same) selection.insert(key.position());
            }
            result.selectionChanged = true;
            m_pressedOnBoard = false; // the release of this click must not reduce the selection again
        }
    }
    if (m_pressedOnBoard && boardActive && !m_dragging) {
        const ImVec2 moved = io.MousePos - m_dragStart;
        if (moved.x * moved.x + moved.y * moved.y > DRAG_THRESHOLD_PX * DRAG_THRESHOLD_PX) m_dragging = true;
    }

    const Box dragBox{ImVec2(std::min(m_dragStart.x, io.MousePos.x), std::min(m_dragStart.y, io.MousePos.y)),
                      ImVec2(std::max(m_dragStart.x, io.MousePos.x), std::max(m_dragStart.y, io.MousePos.y))};
    std::set<KeyPosition> boxed;
    if (m_dragging) {
        for (std::size_t i = 0; i < shapes.size(); ++i) {
            if (shapes[i].overlaps(dragBox)) boxed.insert(layout.keys[i].position());
        }
    }
    const bool additive = io.KeyCtrl || io.KeyShift;

    if (ImGui::IsItemDeactivated() && m_pressedOnBoard) {
        if (m_dragging) {
            if (!additive) selection.clear();
            selection.insert(boxed.begin(), boxed.end());
            result.selectionChanged = true;
        } else if (hoveredIndex >= 0) {
            const KeyPosition key = *result.hovered;
            if (io.KeyCtrl) {
                if (!selection.erase(key)) selection.insert(key);
            } else if (io.KeyShift) {
                selection.insert(key);
            } else {
                selection = {key};
            }
            result.selectionChanged = true;
        } else if (!additive && !selection.empty()) {
            selection.clear(); // click on the case, between the keys
            result.selectionChanged = true;
        }
        m_pressedOnBoard = false;
        m_dragging       = false;
    }
    if (!boardActive && !ImGui::IsItemDeactivated()) {
        m_pressedOnBoard = false;
        m_dragging       = false;
    }
    if (!selection.empty() && !io.WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        selection.clear();
        result.selectionChanged = true;
    }

    // --- keys ---
    for (std::size_t i = 0; i < layout.keys.size(); ++i) {
        const KeyGeometry& key = layout.keys[i];
        const KeyPosition position = key.position();
        KeyLook look;
        look.selected = selection.contains(position) || boxed.contains(position);
        look.hovered  = static_cast<int>(i) == hoveredIndex && !m_dragging;
        drawKey(draw, key, shapes[i], display(position), look, p, unit);
    }

    if (m_dragging) {
        draw->AddRectFilled(dragBox.min, dragBox.max, toImU32(p.accent.withAlpha(45)));
        draw->AddRect(dragBox.min, dragBox.max, toImU32(p.accentText), 0.0f, 0, 1.5f);
    }

    // --- tooltip ---
    if (hoveredIndex >= 0 && !m_dragging && !boardActive) {
        const KeyGeometry& key = layout.keys[static_cast<std::size_t>(hoveredIndex)];
        const KeyDisplay shown = display(key.position());
        const char* name = key.legend[0] != '\0' ? key.legend
                         : key.glyph == KeyGlyph::ArrowUp    ? "Up"
                         : key.glyph == KeyGlyph::ArrowDown  ? "Down"
                         : key.glyph == KeyGlyph::ArrowLeft  ? "Left"
                         : key.glyph == KeyGlyph::ArrowRight ? "Right"
                                                             : "Space";
        ImGui::BeginTooltip();
        ImGui::Text("%s   (matrix row %d, column %d)", name, key.row, key.col);
        if (!shown.note.empty() || shown.hideValue) {
            if (!shown.note.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.textMuted));
                ImGui::TextUnformatted(shown.note.c_str());
                ImGui::PopStyleColor();
            }
        } else {
            ImGui::TextUnformatted(actuation::describe(shown.effective).c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.textMuted));
            ImGui::TextUnformatted(shown.ownSettings ? "Own setting for this key." : "Follows the slot's settings.");
            ImGui::PopStyleColor();
        }
        ImGui::EndTooltip();
    }
    return result;
}

} // namespace ui
