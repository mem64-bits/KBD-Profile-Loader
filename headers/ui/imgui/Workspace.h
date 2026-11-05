#pragma once

#include "ui/AppState.h"
#include "ui/GamepadModel.h"
#include "ui/KeyboardLayout.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>

namespace ui {

    // How controller buttons are pictured on mapped keys: Kenney's CC0 prompts, Zacksly's CC BY 3.0 Xbox Series
    // prompts, or the app's own text chips.
    enum class GlyphStyle : std::uint8_t { Kenney, Zacksly, Drawn };

    // What every page edits, chosen once for the whole window: the slot (picked in the page header), the
    // keyboard drawing and how controller buttons are named (both in Settings). Pages hold a shared pointer to
    // one Workspace, so switching pages keeps the context. UI thread only.
    struct Workspace {
        std::optional<std::size_t>         pickedSlot; // nullopt: follow the slot active on the keyboard
        std::optional<LayoutKind>          layout;     // nullopt: from the connected keyboard (ANSI / ISO)
        std::optional<gamepad::LabelStyle> labels;     // nullopt: from the connected controller
        GlyphStyle                         glyphs{GlyphStyle::Kenney};
        std::array<bool, SLOT_COUNT>       detailsRequested{};  // auto read-back asked for once per slot
        std::array<bool, SLOT_COUNT>       actuationRequested{};

        [[nodiscard]] std::size_t slot(const AppSnapshot& state) const noexcept {
            if (pickedSlot && *pickedSlot < SLOT_COUNT) return *pickedSlot;
            return state.activeSlot >= 0 && state.activeSlot < static_cast<int>(SLOT_COUNT) ? static_cast<std::size_t>(state.activeSlot) : 0;
        }

        [[nodiscard]] LayoutKind keyboardLayout(const AppSnapshot& state) const noexcept {
            if (layout) return *layout;
            return state.device.variant == kbd::KeyboardVariant::Ansi ? LayoutKind::Ansi : LayoutKind::Iso;
        }

        [[nodiscard]] gamepad::LabelStyle labelStyle() const noexcept { return labels.value_or(gamepad::LabelStyle::Xbox); }

        // The display choices as "key=value" lines (ui.ini next to config.ini), and back. Unknown keys and
        // values are ignored, so an old or hand-edited file never breaks start-up.
        [[nodiscard]] std::string saveDisplay() const {
            std::string out = "layout=";
            out += !layout ? "auto" : *layout == LayoutKind::Ansi ? "ansi" : "iso";
            out += "\nlabels=";
            out += !labels ? "auto" : *labels == gamepad::LabelStyle::PlayStation ? "playstation" : "xbox";
            out += "\nprompts=";
            out += glyphs == GlyphStyle::Zacksly ? "zacksly" : glyphs == GlyphStyle::Drawn ? "text" : "kenney";
            out += "\n";
            return out;
        }
        void loadDisplay(const std::string& text) {
            std::size_t start = 0;
            while (start < text.size()) {
                std::size_t end = text.find('\n', start);
                if (end == std::string::npos) end = text.size();
                std::string line = text.substr(start, end - start);
                start = end + 1;
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                const auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                const std::string key = line.substr(0, eq);
                const std::string value = line.substr(eq + 1);
                if (key == "layout") {
                    if (value == "auto") layout.reset();
                    if (value == "ansi") layout = LayoutKind::Ansi;
                    if (value == "iso") layout = LayoutKind::Iso;
                } else if (key == "labels") {
                    if (value == "auto") labels.reset();
                    if (value == "xbox") labels = gamepad::LabelStyle::Xbox;
                    if (value == "playstation") labels = gamepad::LabelStyle::PlayStation;
                } else if (key == "prompts") {
                    if (value == "kenney") glyphs = GlyphStyle::Kenney;
                    if (value == "zacksly") glyphs = GlyphStyle::Zacksly;
                    if (value == "text") glyphs = GlyphStyle::Drawn;
                }
            }
        }
    };

    // Number of keys a slot maps to the game controller (0: a keyboard-only profile).
    [[nodiscard]] inline std::size_t gamepadKeyCount(const SlotDetails& details) {
        return static_cast<std::size_t>(std::count_if(details.keys.begin(), details.keys.end(),
                                                      [](const KeyAdvanced& key) { return key.mode == kbd::AdvancedMode::GAMEPAD; }));
    }

} // namespace ui
