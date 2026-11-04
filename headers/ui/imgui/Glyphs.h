#pragma once

#include "ui/GamepadModel.h"

#include <imgui.h>

#include <cstdint>

// Controller button prompts from Kenney's "Input Prompts" pack (CC0 1.0, www.kenney.nl), packed by hand into
// assets/glyphs/kenney-input-prompts.png: one row of Xbox Series glyphs, one row of PlayStation glyphs, 24 each.
namespace ui::glyphs {

    // Which prompt set to draw. Zacksly's "Xbox Series Button Icons and Controls" and "PS5 Button Icons and
    // Controls" (CC BY 3.0) are packed into assets/glyphs/zacksly-prompts.png like Kenney's: row 0 Xbox,
    // row 1 PS5, same column order.
    enum class Pack : std::uint8_t { Kenney, Zacksly };

    // Draws the prompt for a kbd::GamepadTarget, `size` pixels square, centred on `centre`. Coloured prompts
    // (A B X Y, cross circle square triangle) keep their colours; the white ones are multiplied by `tint`.
    // Returns false when there is no glyph for the target (Guide, the HID-only dial, invalid targets) or the
    // texture is unavailable, so the caller can fall back to drawing it.
    bool drawTarget(ImDrawList* draw, std::uint8_t target, gamepad::LabelStyle style, ImVec2 centre, float size,
                    ImU32 tint = IM_COL32_WHITE, Pack pack = Pack::Kenney);

    // True when drawTarget() has a glyph for the target.
    [[nodiscard]] bool hasGlyph(std::uint8_t target) noexcept;

    // Frees the GPU texture (call before the OpenGL context is destroyed).
    void release();

} // namespace ui::glyphs
