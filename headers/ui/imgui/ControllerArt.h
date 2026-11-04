#pragma once

#include "ui/GamepadModel.h"

#include <imgui.h>

#include <array>
#include <cstdint>
#include <optional>

namespace ui::art {

    // The parts of the controller drawing that light up, can be highlighted and can be clicked.
    enum class Part : std::uint8_t {
        A, B, X, Y,
        LB, RB,
        View, Menu, Guide,
        DpadUp, DpadDown, DpadLeft, DpadRight,
        LT, RT,
        LeftStick, RightStick,
        Count
    };
    inline constexpr std::size_t PART_COUNT = static_cast<std::size_t>(Part::Count);

    [[nodiscard]] std::optional<Part> partFor(gamepad::Button button) noexcept;

    struct Style {
        ImU32 body{IM_COL32(28, 28, 34, 255)};      // silhouette fill
        ImU32 outline{IM_COL32(220, 220, 228, 255)}; // line art
        ImU32 pressed{IM_COL32(124, 77, 255, 255)};  // fill of pressed parts
        ImU32 stickCap{IM_COL32(52, 52, 60, 255)};   // resting stick caps
        ImU32 highlight{IM_COL32(124, 77, 255, 255)};// hover / "this key drives it" ring
        bool  faceColors{true};                      // A green, B red, X blue, Y yellow when pressed
        float stickTravel{1.0f};                     // how far the caps move (1 = like the real thing)
    };

    // Where each part ended up on screen, for hit testing and for drawing extra marks on top.
    struct Layout {
        ImVec2                          min{};
        ImVec2                          max{};
        std::array<ImVec2, PART_COUNT>  centre{};
        std::array<ImVec2, PART_COUNT>  extent{};  // half size of an ellipse that covers the part
        ImVec2                          leftStickRest{};
        ImVec2                          rightStickRest{};
        float                           stickRange{0.0f}; // screen distance of a full stick deflection
    };

    // The Xbox Series-style controller drawing: line art and pressed-state sprites from input-overlay's CC0
    // "xbox-controller" preset (Saikel Orado Liu), with the Xbox logo removed (the Guide button is drawn as a
    // plain ring). The atlas is compiled into the executable and uploaded to the GPU on first use.
    class XboxControllerArt {
    public:
        // Width / height of the drawing.
        static constexpr float ASPECT = 1024.0f / 621.0f;

        // Uploads the atlas if needed (needs the UI thread's OpenGL context). False if it is unavailable.
        static bool ready();
        // Frees the GPU texture (call before the OpenGL context is destroyed).
        static void release();

        // Draws the controller `width` wide with its top-left at `min`. `emphasis` parts get a highlight ring
        // (e.g. the target the hovered key drives).
        static Layout draw(ImDrawList* draw, ImVec2 min, float width, const gamepad::PadState& pad, const Style& style,
                           const std::array<bool, PART_COUNT>& emphasis = {});

        // The part under a screen position, if any (sticks win over the area around them).
        [[nodiscard]] static std::optional<Part> hit(const Layout& layout, ImVec2 position) noexcept;
    };

    // The DualSense (PS5) drawing: line art and pressed-state sprites from input-overlay's CC0 "dualsense" preset
    // (Caro, from Xelu's CC0 controller prompts), with the PlayStation logos removed (the PS button is drawn as a
    // plain ring). Same parts as the Xbox drawing: A = cross, B = circle, X = square, Y = triangle,
    // View = Create, Menu = Options, Guide = PS button.
    class DualSenseArt {
    public:
        static constexpr float ASPECT = 1344.0f / 788.0f;

        static bool ready();
        static void release();
        static Layout draw(ImDrawList* draw, ImVec2 min, float width, const gamepad::PadState& pad, const Style& style,
                           const std::array<bool, PART_COUNT>& emphasis = {});
    };

    // Which drawing to use.
    enum class Model : std::uint8_t { XboxSeries, DualSense };

    [[nodiscard]] inline Model modelFor(const gamepad::LabelStyle style) noexcept {
        return style == gamepad::LabelStyle::PlayStation ? Model::DualSense : Model::XboxSeries;
    }
    [[nodiscard]] inline float aspect(const Model model) noexcept {
        return model == Model::DualSense ? DualSenseArt::ASPECT : XboxControllerArt::ASPECT;
    }
    inline Layout draw(const Model model, ImDrawList* list, const ImVec2 min, const float width, const gamepad::PadState& pad,
                       const Style& style, const std::array<bool, PART_COUNT>& emphasis = {}) {
        return model == Model::DualSense ? DualSenseArt::draw(list, min, width, pad, style, emphasis)
                                         : XboxControllerArt::draw(list, min, width, pad, style, emphasis);
    }
    // The part under a screen position (works for either drawing).
    [[nodiscard]] inline std::optional<Part> hit(const Layout& layout, const ImVec2 position) noexcept {
        return XboxControllerArt::hit(layout, position);
    }

} // namespace ui::art
