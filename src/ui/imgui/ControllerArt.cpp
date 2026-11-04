#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "ui/imgui/ControllerArt.h"
#include "ui/EmbeddedAssets.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <cmath>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace ui::art {

namespace {

    // Everything below is in "drawing units": the line-art body is 1024 x 621 of them. The atlas
    // (assets/controller/xbox-series-atlas.png, 2048 x 1196) holds, at half the resolution of input-overlay's
    // original 2048-wide sheet:
    //   (0, 0)       the body line art          (1024, 0)   the body silhouette
    //   (0, 621)     sprite outlines / letters  (1024, 621) sprite fills, white so they can be tinted
    // Sprites keep the original sheet's coordinates (u, v, w, h) and positions (x, y), halved here.
    constexpr float ATLAS_W = 2048.0f;
    constexpr float ATLAS_H = 1196.0f;
    constexpr float BODY_W  = 1024.0f;
    constexpr float BODY_H  = 621.0f;
    constexpr float FILL_X  = 1024.0f;
    constexpr float SHEET_Y  = 621.0f;

    struct Sprite {
        float u, v, w, h; // original sheet, unpressed state (the pressed state sits right below it)
        float x, y;       // where it goes on the original 2048 x 1242 body
    };

    // xbox-controller.json from input-overlay's presets (CC0).
    constexpr Sprite SPRITE_A{1695, 1854, 176, 154, 1513, 697};
    constexpr Sprite SPRITE_B{1872, 1854, 176, 153, 1679, 560};
    constexpr Sprite SPRITE_Y{1518, 1854, 176, 153, 1533, 442};
    constexpr Sprite SPRITE_X{1341, 1854, 176, 153, 1366, 575};
    constexpr Sprite SPRITE_VIEW{1810, 1243, 111, 99, 814, 601};
    constexpr Sprite SPRITE_MENU{1922, 1243, 111, 99, 1123, 601};
    constexpr Sprite SPRITE_LB{0, 1243, 514, 213, 197, 144};
    constexpr Sprite SPRITE_RB{515, 1243, 514, 213, 1336, 144};
    constexpr Sprite SPRITE_DUP{0, 1854, 332, 267, 572, 818};
    constexpr Sprite SPRITE_DDOWN{333, 1854, 332, 267, 572, 818};
    constexpr Sprite SPRITE_DLEFT{666, 1854, 332, 267, 572, 818};
    constexpr Sprite SPRITE_DRIGHT{999, 1854, 332, 267, 572, 818};
    constexpr Sprite SPRITE_LT{1043, 1243, 260, 272, 223, 0};
    constexpr Sprite SPRITE_RT{1304, 1243, 260, 272, 1565, 0};
    constexpr Sprite SPRITE_STICK{1565, 1243, 244, 244, 0, 0};

    // Stick caps rest in the middle of their wells (the preset puts them a little lower).
    constexpr ImVec2 LEFT_STICK_REST{220.0f, 332.5f};
    constexpr ImVec2 RIGHT_STICK_REST{655.0f, 475.0f};
    constexpr float  STICK_RANGE = 35.0f;
    constexpr ImVec2 GUIDE_CENTRE{512.0f, 223.5f};
    constexpr float  GUIDE_RADIUS = 44.0f;
    constexpr ImVec2 DPAD_CENTRE{369.0f, 475.5f};

    GLuint g_texture = 0;
    bool   g_failed  = false;

    bool upload() {
        if (g_texture != 0) return true;
        if (g_failed) return false;
        g_failed = true; // only try once
        const auto bytes = assets::find("assets/controller/xbox-series-atlas.png");
        if (bytes.empty()) return false;
        SDL_IOStream* io = SDL_IOFromConstMem(bytes.data(), bytes.size());
        if (io == nullptr) return false;
        SDL_Surface* loaded = SDL_LoadPNG_IO(io, true);
        if (loaded == nullptr) return false;
        SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_ABGR8888); // bytes R, G, B, A in memory
        SDL_DestroySurface(loaded);
        if (rgba == nullptr) return false;

        glGenTextures(1, &g_texture);
        glBindTexture(GL_TEXTURE_2D, g_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, rgba->pitch / 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        SDL_DestroySurface(rgba);
        g_failed = false;
        return true;
    }

    ImTextureRef textureRef() {
        return ImTextureRef(static_cast<ImTextureID>(g_texture));
    }

    ImU32 scaleAlpha(const ImU32 color, const float factor) {
        const auto a = static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(factor, 0.0f, 1.0f);
        return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
    }

    class Painter {
    public:
        Painter(ImDrawList* draw, const ImVec2 min, const float scale) : m_draw(draw), m_min(min), m_scale(scale) {}

        [[nodiscard]] ImVec2 at(const ImVec2 unit) const { return m_min + unit * m_scale; }
        [[nodiscard]] float  len(const float unit) const { return unit * m_scale; }

        // Atlas region (in drawing units) -> screen rectangle (in drawing units).
        void image(const ImVec2 atlasMin, const ImVec2 size, const ImVec2 where, const ImU32 tint) const {
            const ImVec2 uv0(atlasMin.x / ATLAS_W, atlasMin.y / ATLAS_H);
            const ImVec2 uv1((atlasMin.x + size.x) / ATLAS_W, (atlasMin.y + size.y) / ATLAS_H);
            m_draw->AddImage(textureRef(), at(where), at(where + size), uv0, uv1, tint);
        }

        // A sprite in one of its two states; `fill` picks the tintable fill layer instead of the line layer.
        void sprite(const Sprite& s, const bool pressed, const bool fill, const ImU32 tint, const ImVec2 offset = {}) const {
            const float v = pressed ? s.v + s.h + 1.0f : s.v;
            const ImVec2 atlas(s.u * 0.5f + (fill ? FILL_X : 0.0f), SHEET_Y + (v - 1242.0f) * 0.5f);
            image(atlas, ImVec2(s.w * 0.5f, s.h * 0.5f), ImVec2(s.x * 0.5f, s.y * 0.5f) + offset, tint);
        }

        // Only the part of a sprite between `from` and `to` (0 = top, 1 = bottom), for analog triggers.
        void spriteBand(const Sprite& s, const bool fill, const ImU32 tint, const float from, const float to) const {
            const float v = s.v + s.h + 1.0f; // pressed state
            const float top = std::clamp(from, 0.0f, 1.0f);
            const float bottom = std::clamp(to, 0.0f, 1.0f);
            if (bottom <= top) return;
            const ImVec2 atlas(s.u * 0.5f + (fill ? FILL_X : 0.0f), SHEET_Y + (v - 1242.0f) * 0.5f + s.h * 0.5f * top);
            image(atlas, ImVec2(s.w * 0.5f, s.h * 0.5f * (bottom - top)), ImVec2(s.x * 0.5f, s.y * 0.5f + s.h * 0.5f * top), tint);
        }

        [[nodiscard]] ImDrawList* list() const { return m_draw; }

    private:
        ImDrawList* m_draw;
        ImVec2      m_min;
        float       m_scale;
    };

    ImVec2 spriteCentre(const Sprite& s) {
        return ImVec2((s.x + s.w * 0.5f) * 0.5f, (s.y + s.h * 0.5f) * 0.5f);
    }
    ImVec2 spriteExtent(const Sprite& s) {
        return ImVec2(s.w * 0.25f, s.h * 0.25f);
    }

    struct FacePart {
        Part           part;
        const Sprite*  sprite;
        gamepad::Button button;
        ImU32          color;
    };

} // namespace

std::optional<Part> partFor(const gamepad::Button button) noexcept {
    using gamepad::Button;
    switch (button) {
        case Button::South: return Part::A;
        case Button::East: return Part::B;
        case Button::West: return Part::X;
        case Button::North: return Part::Y;
        case Button::Back: return Part::View;
        case Button::Guide: return Part::Guide;
        case Button::Start: return Part::Menu;
        case Button::LeftStick: return Part::LeftStick;
        case Button::RightStick: return Part::RightStick;
        case Button::LeftShoulder: return Part::LB;
        case Button::RightShoulder: return Part::RB;
        case Button::DpadUp: return Part::DpadUp;
        case Button::DpadDown: return Part::DpadDown;
        case Button::DpadLeft: return Part::DpadLeft;
        case Button::DpadRight: return Part::DpadRight;
        default: return std::nullopt;
    }
}

bool XboxControllerArt::ready() {
    return upload();
}

void XboxControllerArt::release() {
    if (g_texture != 0) glDeleteTextures(1, &g_texture);
    g_texture = 0;
    g_failed  = false;
}

Layout XboxControllerArt::draw(ImDrawList* drawList, const ImVec2 min, const float width, const gamepad::PadState& pad,
                               const Style& style, const std::array<bool, PART_COUNT>& emphasis) {
    using gamepad::Axis;
    using gamepad::Button;

    const float scale = width / BODY_W;
    Layout layout;
    layout.min = min;
    layout.max = min + ImVec2(BODY_W, BODY_H) * scale;
    layout.stickRange = STICK_RANGE * scale * style.stickTravel;

    const Painter paint(drawList, min, scale);
    const auto setPart = [&](const Part part, const ImVec2 centreUnits, const ImVec2 extentUnits) {
        layout.centre[static_cast<std::size_t>(part)] = paint.at(centreUnits);
        layout.extent[static_cast<std::size_t>(part)] = extentUnits * scale;
    };

    // Hit-test / highlight geometry is known without the texture, so callers still get a layout.
    setPart(Part::A, spriteCentre(SPRITE_A), spriteExtent(SPRITE_A));
    setPart(Part::B, spriteCentre(SPRITE_B), spriteExtent(SPRITE_B));
    setPart(Part::X, spriteCentre(SPRITE_X), spriteExtent(SPRITE_X));
    setPart(Part::Y, spriteCentre(SPRITE_Y), spriteExtent(SPRITE_Y));
    setPart(Part::View, spriteCentre(SPRITE_VIEW), spriteExtent(SPRITE_VIEW));
    setPart(Part::Menu, spriteCentre(SPRITE_MENU), spriteExtent(SPRITE_MENU));
    setPart(Part::Guide, GUIDE_CENTRE, ImVec2(GUIDE_RADIUS, GUIDE_RADIUS));
    // Bumpers: only the outer half of each sprite is the bumper itself.
    setPart(Part::LB, spriteCentre(SPRITE_LB) + ImVec2(-20.0f, 4.0f), ImVec2(80.0f, 26.0f));
    setPart(Part::RB, spriteCentre(SPRITE_RB) + ImVec2(20.0f, 4.0f), ImVec2(80.0f, 26.0f));
    setPart(Part::LT, spriteCentre(SPRITE_LT) + ImVec2(0.0f, -8.0f), ImVec2(30.0f, 40.0f));
    setPart(Part::RT, spriteCentre(SPRITE_RT) + ImVec2(0.0f, -8.0f), ImVec2(30.0f, 40.0f));
    setPart(Part::DpadUp, DPAD_CENTRE + ImVec2(0.0f, -38.0f), ImVec2(20.0f, 24.0f));
    setPart(Part::DpadDown, DPAD_CENTRE + ImVec2(0.0f, 38.0f), ImVec2(20.0f, 24.0f));
    setPart(Part::DpadLeft, DPAD_CENTRE + ImVec2(-45.0f, 0.0f), ImVec2(26.0f, 18.0f));
    setPart(Part::DpadRight, DPAD_CENTRE + ImVec2(45.0f, 0.0f), ImVec2(26.0f, 18.0f));
    layout.leftStickRest  = paint.at(LEFT_STICK_REST);
    layout.rightStickRest = paint.at(RIGHT_STICK_REST);
    const ImVec2 leftOffset(pad.axis(Axis::LeftX) * STICK_RANGE * style.stickTravel, -pad.axis(Axis::LeftY) * STICK_RANGE * style.stickTravel);
    const ImVec2 rightOffset(pad.axis(Axis::RightX) * STICK_RANGE * style.stickTravel, -pad.axis(Axis::RightY) * STICK_RANGE * style.stickTravel);
    setPart(Part::LeftStick, LEFT_STICK_REST + leftOffset, ImVec2(50.0f, 50.0f));
    setPart(Part::RightStick, RIGHT_STICK_REST + rightOffset, ImVec2(50.0f, 50.0f));

    if (!upload()) {
        drawList->AddRect(layout.min, layout.max, style.outline, 12.0f * scale);
        return layout;
    }

    // Silhouette, then the line art.
    paint.image(ImVec2(FILL_X, 0.0f), ImVec2(BODY_W, BODY_H), ImVec2(0, 0), style.body);

    // Triggers: the pressed fill rises from the bottom with the pull, then the outline on top.
    const auto trigger = [&](const Sprite& s, const float value) {
        if (value > 0.01f) {
            paint.spriteBand(s, true, style.pressed, 1.0f - value, 1.0f);
        }
    };
    trigger(SPRITE_LT, pad.axis(Axis::LeftTrigger));
    trigger(SPRITE_RT, pad.axis(Axis::RightTrigger));

    // Bumpers sit under the body's top edge in the line art: draw their fills first.
    if (pad.pressed(Button::LeftShoulder)) paint.sprite(SPRITE_LB, true, true, style.pressed);
    if (pad.pressed(Button::RightShoulder)) paint.sprite(SPRITE_RB, true, true, style.pressed);

    paint.image(ImVec2(0, 0), ImVec2(BODY_W, BODY_H), ImVec2(0, 0), style.outline);
    if (pad.pressed(Button::LeftShoulder)) paint.sprite(SPRITE_LB, true, false, style.outline);
    if (pad.pressed(Button::RightShoulder)) paint.sprite(SPRITE_RB, true, false, style.outline);
    if (pad.axis(Axis::LeftTrigger) > 0.01f) paint.sprite(SPRITE_LT, true, false, style.outline);
    if (pad.axis(Axis::RightTrigger) > 0.01f) paint.sprite(SPRITE_RT, true, false, style.outline);

    // Face buttons.
    const ImU32 faceA = style.faceColors ? IM_COL32(64, 185, 80, 255) : style.pressed;
    const ImU32 faceB = style.faceColors ? IM_COL32(236, 82, 72, 255) : style.pressed;
    const ImU32 faceX = style.faceColors ? IM_COL32(60, 140, 250, 255) : style.pressed;
    const ImU32 faceY = style.faceColors ? IM_COL32(232, 184, 60, 255) : style.pressed;
    const FacePart faces[] = {
        {Part::A, &SPRITE_A, Button::South, faceA},
        {Part::B, &SPRITE_B, Button::East, faceB},
        {Part::X, &SPRITE_X, Button::West, faceX},
        {Part::Y, &SPRITE_Y, Button::North, faceY},
        {Part::View, &SPRITE_VIEW, Button::Back, style.pressed},
        {Part::Menu, &SPRITE_MENU, Button::Start, style.pressed},
    };
    for (const FacePart& face : faces) {
        if (!pad.pressed(face.button)) continue;
        paint.sprite(*face.sprite, true, true, face.color);
        paint.sprite(*face.sprite, true, false, style.outline);
    }

    // D-pad arms.
    const std::pair<const Sprite*, Button> arms[] = {
        {&SPRITE_DUP, Button::DpadUp}, {&SPRITE_DDOWN, Button::DpadDown}, {&SPRITE_DLEFT, Button::DpadLeft}, {&SPRITE_DRIGHT, Button::DpadRight}};
    for (const auto& [sprite, button] : arms) {
        if (pad.pressed(button)) paint.sprite(*sprite, true, true, style.pressed);
    }

    // Guide: a plain ring (the Xbox logo is a trademark and is not part of the artwork).
    {
        const ImVec2 c = paint.at(GUIDE_CENTRE);
        const float r = paint.len(GUIDE_RADIUS);
        if (pad.pressed(Button::Guide)) drawList->AddCircleFilled(c, r, style.pressed, 48);
        drawList->AddCircle(c, r, style.outline, 48, paint.len(4.0f));
        drawList->AddCircle(c, r * 0.42f, scaleAlpha(style.outline, 0.55f), 32, paint.len(3.0f));
    }

    // Stick caps, moved by the stick position.
    const auto stick = [&](const ImVec2 rest, const ImVec2 offset, const bool clicked) {
        const ImVec2 half(SPRITE_STICK.w * 0.25f, SPRITE_STICK.h * 0.25f);
        const ImVec2 topLeft = rest + offset - half;
        const ImVec2 shift = topLeft - ImVec2(SPRITE_STICK.x, SPRITE_STICK.y) * 0.5f;
        if (clicked) {
            paint.sprite(SPRITE_STICK, true, true, style.pressed, shift);
            paint.sprite(SPRITE_STICK, true, false, style.outline, shift);
        } else {
            drawList->AddCircleFilled(paint.at(rest + offset), paint.len(half.x * 0.86f), style.stickCap, 48);
            paint.sprite(SPRITE_STICK, false, false, style.outline, shift);
        }
    };
    stick(LEFT_STICK_REST, leftOffset, pad.pressed(Button::LeftStick));
    stick(RIGHT_STICK_REST, rightOffset, pad.pressed(Button::RightStick));

    // Highlight rings.
    for (std::size_t i = 0; i < PART_COUNT; ++i) {
        if (!emphasis[i]) continue;
        const ImVec2 c = layout.centre[i];
        const ImVec2 e = layout.extent[i] + ImVec2(paint.len(8.0f), paint.len(8.0f));
        drawList->AddEllipse(c, e, style.highlight, 0.0f, 40, std::max(2.0f, paint.len(5.0f)));
    }
    return layout;
}

std::optional<Part> XboxControllerArt::hit(const Layout& layout, const ImVec2 position) noexcept {
    std::optional<Part> best;
    float bestDistance = 1.0f;
    for (std::size_t i = 0; i < PART_COUNT; ++i) {
        const ImVec2 e = layout.extent[i];
        if (e.x <= 0.0f || e.y <= 0.0f) continue;
        const ImVec2 d = position - layout.centre[i];
        const float distance = (d.x * d.x) / (e.x * e.x) + (d.y * d.y) / (e.y * e.y);
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = static_cast<Part>(i);
        }
    }
    return best;
}

} // namespace ui::art
