#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "ui/imgui/ControllerArt.h"
#include "ui/EmbeddedAssets.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace ui::art {

namespace {

    // Drawing units are half of the preset's sheet pixels: the body is 672 x 394 of them. The atlas
    // (assets/controller/dualsense-atlas.png, 2090 x 618, made by tools/make_dualsense_atlas.py from dualsense.png) holds
    // the sheet's white line art on the left and its fills (pressed states, body silhouette) on the right.
    constexpr float ATLAS_W = 2090.0f;
    constexpr float ATLAS_H = 618.0f;
    constexpr float FILL_X  = 1045.0f;
    constexpr float BODY_W  = 672.0f;
    constexpr float BODY_H  = 394.0f;

    struct Sprite {
        float u, v, w, h; // sheet, unpressed state (the pressed state sits 3 px below it)
        float x, y;       // position on the sheet's 1344 x 788 body
    };

    // dualsense.json from input-overlay's presets (CC0). Left / right follow where each sprite is drawn.
    constexpr Sprite CROSS{0, 792, 99, 91, 1047, 431};
    constexpr Sprite CIRCLE{305, 792, 98, 90, 1150, 345};
    constexpr Sprite SQUARE{102, 793, 98, 90, 946, 349};
    constexpr Sprite TRIANGLE{203, 792, 99, 91, 1050, 257};
    constexpr Sprite CREATE{874, 792, 45, 66, 332, 225};
    constexpr Sprite OPTIONS{827, 792, 44, 66, 968, 225};
    constexpr Sprite L1{409, 792, 206, 108, 146, 75};
    constexpr Sprite R1{618, 792, 206, 108, 992, 75};
    constexpr Sprite L2{1112, 792, 187, 104, 167, 0};
    constexpr Sprite R2{922, 792, 187, 104, 990, 0};
    constexpr Sprite STICK{1359, 1, 157, 156, 0, 0};
    // D-pad: nine states side by side (none, left, right, up, down, up-left, up-right, down-left, down-right).
    constexpr float DPAD_U = 1.0f, DPAD_V = 1013.0f, DPAD_W = 229.0f, DPAD_H = 220.0f, DPAD_STRIDE = 232.0f;
    constexpr ImVec2 DPAD_POS{136.0f, 281.0f};

    constexpr ImVec2 LEFT_STICK_REST{(389.0f + 78.5f) * 0.5f, (517.0f + 78.0f) * 0.5f};
    constexpr ImVec2 RIGHT_STICK_REST{(810.0f + 78.5f) * 0.5f, (516.0f + 78.0f) * 0.5f};
    constexpr float  STICK_RANGE = 15.0f;
    constexpr ImVec2 PS_CENTRE{(634.0f + 43.0f) * 0.5f, (507.0f + 27.0f) * 0.5f};
    constexpr float  PS_RADIUS = 13.0f;

    GLuint g_texture = 0;
    bool   g_failed  = false;

    bool upload() {
        if (g_texture != 0) return true;
        if (g_failed) return false;
        g_failed = true;
        const auto bytes = assets::find("assets/controller/dualsense-atlas.png");
        if (bytes.empty()) return false;
        SDL_IOStream* io = SDL_IOFromConstMem(bytes.data(), bytes.size());
        if (io == nullptr) return false;
        SDL_Surface* loaded = SDL_LoadPNG_IO(io, true);
        if (loaded == nullptr) return false;
        SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_ABGR8888);
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

    class Painter {
    public:
        Painter(ImDrawList* draw, const ImVec2 min, const float scale) : m_draw(draw), m_min(min), m_scale(scale) {}

        [[nodiscard]] ImVec2 at(const ImVec2 unit) const { return m_min + unit * m_scale; }
        [[nodiscard]] float  len(const float unit) const { return unit * m_scale; }

        // Atlas region (atlas pixels) -> screen rectangle (drawing units).
        void image(const ImVec2 atlasMin, const ImVec2 size, const ImVec2 where, const ImU32 tint) const {
            const ImVec2 uv0(atlasMin.x / ATLAS_W, atlasMin.y / ATLAS_H);
            const ImVec2 uv1((atlasMin.x + size.x) / ATLAS_W, (atlasMin.y + size.y) / ATLAS_H);
            m_draw->AddImage(ImTextureRef(static_cast<ImTextureID>(g_texture)), at(where), at(where + size), uv0, uv1, tint);
        }

        // A sheet rectangle drawn at a sheet position, from the line or the fill layer.
        void sheet(const float u, const float v, const float w, const float h, const ImVec2 sheetPos, const bool fill,
                   const ImU32 tint, const float from = 0.0f, const float to = 1.0f) const {
            const float top = std::clamp(from, 0.0f, 1.0f);
            const float bottom = std::clamp(to, 0.0f, 1.0f);
            if (bottom <= top) return;
            image(ImVec2(u * 0.5f + (fill ? FILL_X : 0.0f), (v + h * top) * 0.5f), ImVec2(w * 0.5f, h * (bottom - top) * 0.5f),
                  ImVec2(sheetPos.x * 0.5f, (sheetPos.y + h * top) * 0.5f), tint);
        }

        void sprite(const Sprite& s, const bool pressed, const bool fill, const ImU32 tint, const ImVec2 sheetPos) const {
            sheet(s.u, pressed ? s.v + s.h + 3.0f : s.v, s.w, s.h, sheetPos, fill, tint);
        }
        void sprite(const Sprite& s, const bool pressed, const bool fill, const ImU32 tint) const {
            sprite(s, pressed, fill, tint, ImVec2(s.x, s.y));
        }

    private:
        ImDrawList* m_draw;
        ImVec2      m_min;
        float       m_scale;
    };

    ImVec2 centreOf(const Sprite& s) { return ImVec2((s.x + s.w * 0.5f) * 0.5f, (s.y + s.h * 0.5f) * 0.5f); }
    ImVec2 extentOf(const Sprite& s) { return ImVec2(s.w * 0.25f, s.h * 0.25f); }

} // namespace

bool DualSenseArt::ready() {
    return upload();
}

void DualSenseArt::release() {
    if (g_texture != 0) glDeleteTextures(1, &g_texture);
    g_texture = 0;
    g_failed  = false;
}

Layout DualSenseArt::draw(ImDrawList* drawList, const ImVec2 min, const float width, const gamepad::PadState& pad,
                          const Style& style, const std::array<bool, PART_COUNT>& emphasis) {
    using gamepad::Axis;
    using gamepad::Button;

    const float scale = width / BODY_W;
    Layout layout;
    layout.min = min;
    layout.max = min + ImVec2(BODY_W, BODY_H) * scale;
    layout.stickRange = STICK_RANGE * scale * style.stickTravel;

    const Painter paint(drawList, min, scale);
    const auto setPart = [&](const Part part, const ImVec2 centre, const ImVec2 extent) {
        layout.centre[static_cast<std::size_t>(part)] = paint.at(centre);
        layout.extent[static_cast<std::size_t>(part)] = extent * scale;
    };
    const ImVec2 dpad(DPAD_POS.x * 0.5f + DPAD_W * 0.25f, DPAD_POS.y * 0.5f + DPAD_H * 0.25f);
    setPart(Part::A, centreOf(CROSS), extentOf(CROSS));
    setPart(Part::B, centreOf(CIRCLE), extentOf(CIRCLE));
    setPart(Part::X, centreOf(SQUARE), extentOf(SQUARE));
    setPart(Part::Y, centreOf(TRIANGLE), extentOf(TRIANGLE));
    setPart(Part::View, centreOf(CREATE), extentOf(CREATE) + ImVec2(4.0f, 2.0f));
    setPart(Part::Menu, centreOf(OPTIONS), extentOf(OPTIONS) + ImVec2(4.0f, 2.0f));
    setPart(Part::Guide, PS_CENTRE, ImVec2(PS_RADIUS, PS_RADIUS));
    setPart(Part::LB, centreOf(L1), ImVec2(44.0f, 16.0f));
    setPart(Part::RB, centreOf(R1), ImVec2(44.0f, 16.0f));
    setPart(Part::LT, centreOf(L2) + ImVec2(0.0f, -4.0f), ImVec2(36.0f, 18.0f));
    setPart(Part::RT, centreOf(R2) + ImVec2(0.0f, -4.0f), ImVec2(36.0f, 18.0f));
    setPart(Part::DpadUp, dpad + ImVec2(0.0f, -32.0f), ImVec2(15.0f, 18.0f));
    setPart(Part::DpadDown, dpad + ImVec2(0.0f, 32.0f), ImVec2(15.0f, 18.0f));
    setPart(Part::DpadLeft, dpad + ImVec2(-32.0f, 0.0f), ImVec2(18.0f, 15.0f));
    setPart(Part::DpadRight, dpad + ImVec2(32.0f, 0.0f), ImVec2(18.0f, 15.0f));
    layout.leftStickRest  = paint.at(LEFT_STICK_REST);
    layout.rightStickRest = paint.at(RIGHT_STICK_REST);
    const float travel = STICK_RANGE * style.stickTravel;
    const ImVec2 leftOffset(pad.axis(Axis::LeftX) * travel, -pad.axis(Axis::LeftY) * travel);
    const ImVec2 rightOffset(pad.axis(Axis::RightX) * travel, -pad.axis(Axis::RightY) * travel);
    setPart(Part::LeftStick, LEFT_STICK_REST + leftOffset, ImVec2(36.0f, 36.0f));
    setPart(Part::RightStick, RIGHT_STICK_REST + rightOffset, ImVec2(36.0f, 36.0f));

    if (!upload()) {
        drawList->AddRect(layout.min, layout.max, style.outline, 12.0f * scale);
        return layout;
    }

    // Silhouette, triggers (fill rising with the pull), bumper fills, then the line art on top.
    paint.image(ImVec2(FILL_X, 0.0f), ImVec2(BODY_W, BODY_H), ImVec2(0, 0), style.body);
    const auto trigger = [&](const Sprite& s, const float value) {
        if (value <= 0.01f) return;
        paint.sheet(s.u, s.v + s.h + 3.0f, s.w, s.h, ImVec2(s.x, s.y), true, style.pressed, 1.0f - value, 1.0f);
        paint.sprite(s, true, false, style.outline);
    };
    trigger(L2, pad.axis(Axis::LeftTrigger));
    trigger(R2, pad.axis(Axis::RightTrigger));
    if (pad.pressed(Button::LeftShoulder)) paint.sprite(L1, true, true, style.pressed);
    if (pad.pressed(Button::RightShoulder)) paint.sprite(R1, true, true, style.pressed);
    paint.image(ImVec2(0, 0), ImVec2(BODY_W, BODY_H), ImVec2(0, 0), style.outline);
    if (pad.pressed(Button::LeftShoulder)) paint.sprite(L1, true, false, style.outline);
    if (pad.pressed(Button::RightShoulder)) paint.sprite(R1, true, false, style.outline);

    // Face buttons in PlayStation's colours when pressed (cross blue, circle red, square pink, triangle green).
    const auto colour = [&](const ImU32 c) { return style.faceColors ? c : style.pressed; };
    const struct { const Sprite* sprite; Button button; ImU32 color; } faces[] = {
        {&CROSS, Button::South, colour(IM_COL32(110, 160, 240, 255))},
        {&CIRCLE, Button::East, colour(IM_COL32(236, 82, 82, 255))},
        {&SQUARE, Button::West, colour(IM_COL32(222, 110, 190, 255))},
        {&TRIANGLE, Button::North, colour(IM_COL32(64, 200, 160, 255))},
        {&CREATE, Button::Back, style.pressed},
        {&OPTIONS, Button::Start, style.pressed},
    };
    for (const auto& face : faces) {
        if (!pad.pressed(face.button)) continue;
        paint.sprite(*face.sprite, true, true, face.color);
        paint.sprite(*face.sprite, true, false, style.outline);
    }

    // D-pad: the state image for the pressed directions (diagonals have their own states).
    {
        const bool up = pad.pressed(Button::DpadUp), down = pad.pressed(Button::DpadDown);
        const bool left = pad.pressed(Button::DpadLeft), right = pad.pressed(Button::DpadRight);
        int state = 0;
        if (up && left) state = 5;
        else if (up && right) state = 6;
        else if (down && left) state = 7;
        else if (down && right) state = 8;
        else if (left) state = 1;
        else if (right) state = 2;
        else if (up) state = 3;
        else if (down) state = 4;
        if (state != 0) {
            const float u = DPAD_U + DPAD_STRIDE * static_cast<float>(state);
            paint.sheet(u, DPAD_V, DPAD_W, DPAD_H, DPAD_POS, true, style.pressed);
            paint.sheet(u, DPAD_V, DPAD_W, DPAD_H, DPAD_POS, false, style.outline);
        }
    }

    // PS button: a plain ring (the PlayStation logo is a trademark and is not part of the artwork).
    {
        const ImVec2 c = paint.at(PS_CENTRE);
        const float r = paint.len(PS_RADIUS);
        if (pad.pressed(Button::Guide)) drawList->AddCircleFilled(c, r, style.pressed, 40);
        drawList->AddCircle(c, r, style.outline, 40, std::max(1.0f, paint.len(2.0f)));
    }

    // Stick caps, moved by the stick position.
    const auto stick = [&](const ImVec2 rest, const ImVec2 offset, const bool clicked) {
        const ImVec2 sheetPos = (rest + offset) * 2.0f - ImVec2(STICK.w, STICK.h) * 0.5f;
        if (clicked) {
            paint.sprite(STICK, true, true, style.pressed, sheetPos);
            paint.sprite(STICK, true, false, style.outline, sheetPos);
        } else {
            drawList->AddCircleFilled(paint.at(rest + offset), paint.len(STICK.w * 0.25f * 0.9f), style.stickCap, 40);
            paint.sprite(STICK, false, false, style.outline, sheetPos);
        }
    };
    stick(LEFT_STICK_REST, leftOffset, pad.pressed(Button::LeftStick));
    stick(RIGHT_STICK_REST, rightOffset, pad.pressed(Button::RightStick));

    for (std::size_t i = 0; i < PART_COUNT; ++i) {
        if (!emphasis[i]) continue;
        const ImVec2 e = layout.extent[i] + ImVec2(paint.len(5.0f), paint.len(5.0f));
        drawList->AddEllipse(layout.centre[i], e, style.highlight, 0.0f, 40, std::max(2.0f, paint.len(3.0f)));
    }
    return layout;
}

} // namespace ui::art
