#include "ui/imgui/Glyphs.h"
#include "kbd/GameController.h"
#include "ui/EmbeddedAssets.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <array>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace ui::glyphs {

namespace {

    constexpr int COLUMNS = 24;
    constexpr int NONE    = -1;

    // Atlas column of each glyph (same order in both rows).
    enum Column : int {
        FACE_A = 0, FACE_B, FACE_X, FACE_Y, BUMPER_L, BUMPER_R, TRIGGER_L, TRIGGER_R, CLICK_L, CLICK_R, VIEW, MENU,
        DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT,
        LS_UP, LS_DOWN, LS_LEFT, LS_RIGHT, RS_UP, RS_DOWN, RS_LEFT, RS_RIGHT
    };

    // kbd::GamepadTarget -> atlas column. Buttons follow the firmware's XInput order:
    // A B X Y LB RB Back Start LS RS Up Down Left Right Guide.
    int columnFor(const std::uint8_t target) noexcept {
        namespace T = kbd::GamepadTarget;
        switch (target) {
            case T::LEFT_STICK_LEFT:   return LS_LEFT;
            case T::LEFT_STICK_RIGHT:  return LS_RIGHT;
            case T::LEFT_STICK_DOWN:   return LS_DOWN;
            case T::LEFT_STICK_UP:     return LS_UP;
            case T::LEFT_TRIGGER:      return TRIGGER_L;
            case T::RIGHT_TRIGGER:     return TRIGGER_R;
            case T::RIGHT_STICK_LEFT:  return RS_LEFT;
            case T::RIGHT_STICK_RIGHT: return RS_RIGHT;
            case T::RIGHT_STICK_DOWN:  return RS_DOWN;
            case T::RIGHT_STICK_UP:    return RS_UP;
            default: break;
        }
        if (!T::isButton(target)) return NONE;
        static constexpr std::array<int, 15> BUTTONS{FACE_A, FACE_B, FACE_X, FACE_Y, BUMPER_L, BUMPER_R, VIEW, MENU,
                                                     CLICK_L, CLICK_R, DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT, NONE};
        const auto n = static_cast<std::size_t>(target - T::BUTTON_BASE);
        return n < BUTTONS.size() ? BUTTONS[n] : NONE;
    }

    struct Atlas {
        const char* path;
        GLuint      texture{0};
        bool        failed{false};
    };
    Atlas g_kenney{"assets/glyphs/kenney-input-prompts.png"};
    Atlas g_zacksly{"assets/glyphs/zacksly-prompts.png"};

    bool upload(Atlas& atlas) {
        GLuint& g_texture = atlas.texture;
        bool&   g_failed  = atlas.failed;
        if (g_texture != 0) return true;
        if (g_failed) return false;
        g_failed = true; // only try once
        const auto bytes = assets::find(atlas.path);
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

} // namespace

bool hasGlyph(const std::uint8_t target) noexcept {
    return columnFor(target) != NONE;
}

bool drawTarget(ImDrawList* draw, const std::uint8_t target, const gamepad::LabelStyle style, const ImVec2 centre,
                const float size, const ImU32 tint, const Pack pack) {
    const int column = columnFor(target);
    if (column == NONE || draw == nullptr) return false;
    const bool playStation = style == gamepad::LabelStyle::PlayStation;
    const bool zacksly = pack == Pack::Zacksly && upload(g_zacksly);
    if (!zacksly && !upload(g_kenney)) return false;

    constexpr float rows = 2.0f;
    const float row = playStation ? 1.0f : 0.0f;
    const ImVec2 uv0(static_cast<float>(column) / COLUMNS, row / rows);
    const ImVec2 uv1(static_cast<float>(column + 1) / COLUMNS, (row + 1.0f) / rows);
    const float h = size * 0.5f;
    ImU32 colour = tint;
    if (column <= FACE_Y) {
        // Kenney's face buttons carry their own colours; Zacksly's are white, so give them the console's ones
        // (Xbox: A green, B red, X blue, Y yellow; PlayStation: cross blue, circle red, square pink, triangle green).
        static constexpr std::array<ImU32, 4> XBOX{IM_COL32(16, 180, 74, 255), IM_COL32(232, 52, 52, 255),
                                                   IM_COL32(40, 120, 230, 255), IM_COL32(240, 180, 20, 255)};
        static constexpr std::array<ImU32, 4> PLAYSTATION{IM_COL32(110, 160, 240, 255), IM_COL32(236, 82, 82, 255),
                                                          IM_COL32(222, 110, 190, 255), IM_COL32(64, 200, 160, 255)};
        const auto i = static_cast<std::size_t>(column);
        colour = !zacksly ? IM_COL32_WHITE : playStation ? PLAYSTATION[i] : XBOX[i];
        colour = (colour & ~IM_COL32_A_MASK) | (tint & IM_COL32_A_MASK); // the tint's alpha still fades them
    }
    const GLuint texture = zacksly ? g_zacksly.texture : g_kenney.texture;
    draw->AddImage(ImTextureRef(static_cast<ImTextureID>(texture)), ImVec2(centre.x - h, centre.y - h),
                   ImVec2(centre.x + h, centre.y + h), uv0, uv1, colour);
    return true;
}

void release() {
    for (Atlas* atlas : {&g_kenney, &g_zacksly}) {
        if (atlas->texture != 0) glDeleteTextures(1, &atlas->texture);
        atlas->texture = 0;
        atlas->failed  = false;
    }
}

} // namespace ui::glyphs
