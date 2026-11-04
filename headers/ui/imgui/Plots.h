#pragma once

#include "kbd/FirmwareModel.h"
#include "kbd/GameController.h"
#include "ui/Theme.h"

#include <imgui.h>

#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

// Diagrams drawn with ImPlot, styled like the rest of the app.
namespace ui::plots {

    // Colours, fonts and paddings of ImPlot from the theme; call once per frame before the first plot.
    void applyStyle(const ThemePalette& palette);

    // One key-travel reading: seconds (any clock) and depth in 0.1 mm.
    struct TravelSample {
        double time{0.0};
        float  tenths{0.0f};
    };

    // A rolling window of live readings for one key.
    class TravelHistory {
    public:
        void push(double time, float tenths);
        void clear() noexcept { m_samples.clear(); }
        [[nodiscard]] const std::deque<TravelSample>& samples() const noexcept { return m_samples; }
        [[nodiscard]] bool empty() const noexcept { return m_samples.empty(); }

    private:
        std::deque<TravelSample> m_samples;
    };

    // A made-up press pattern (tap, Rapid Trigger flutter, half release, re-press, full release) that loops
    // every 4 s, so the diagram explains the settings even without a keyboard.
    [[nodiscard]] std::vector<TravelSample> demoPattern(double now, double window);

    struct TravelPlotOptions {
        kbd::model::TriggerSettings settings;
        std::optional<std::uint8_t> dksShallowTenths; // dynamic keystroke points to mark, if any
        std::optional<std::uint8_t> dksDeepTenths;
        bool                        live{false};      // samples are real readings (else the demo pattern)
        double                      window{4.0};      // seconds shown
    };

    // Key depth over time (0 mm at the top, like a key going down), the actuation and release points, the
    // moving Rapid Trigger points, and shading wherever the firmware would report the key as pressed. The
    // press / release logic is kbd::model::KeyTriggerModel, i.e. the keyboard's own.
    void travelPlot(const char* id, std::span<const TravelSample> samples, double now, const TravelPlotOptions& options,
                    ImVec2 size, const ThemePalette& palette);

    // The response curve (key travel -> stick deflection) with draggable points. `liveTenths` marks a key's
    // live travel, `reported` the deflection the controller reports for it (0 .. 1). Returns true when a point
    // was dragged.
    bool curvePlot(const char* id, kbd::ResponseCurve& curve, std::optional<float> liveTenths, std::optional<float> reported,
                   ImVec2 size, const ThemePalette& palette);

} // namespace ui::plots
