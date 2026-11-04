#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "ui/imgui/Plots.h"
#include "ui/imgui/ImGuiTheme.h"

#include <implot.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ui::plots {

namespace {

    using kbd::model::FULL_TRAVEL;
    using kbd::model::KeyTriggerModel;
    using kbd::model::TRAVEL_SCALE;

    ImVec4 vec(const Color& color, const float alpha = 1.0f) {
        ImVec4 v = toImVec4(color);
        v.w *= alpha;
        return v;
    }

    constexpr ImPlotFlags PLOT_FLAGS = ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText | ImPlotFlags_NoTitle;
    constexpr ImPlotAxisFlags AXIS_FLAGS = ImPlotAxisFlags_NoMenus | ImPlotAxisFlags_NoSideSwitch | ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_Lock;

    // Smoothstep between key frames.
    float ease(const float a, const float b, const float t) {
        const float s = t * t * (3.0f - 2.0f * t);
        return a + (b - a) * s;
    }

} // namespace

void applyStyle(const ThemePalette& p) {
    ImPlotStyle& s = ImPlot::GetStyle();
    const float em = ImGui::GetFontSize();
    s.Colors[ImPlotCol_FrameBg]      = ImVec4(0, 0, 0, 0);
    s.Colors[ImPlotCol_PlotBg]       = vec(p.background.mix(p.surface, 0.55f));
    s.Colors[ImPlotCol_PlotBorder]   = vec(p.border);
    s.Colors[ImPlotCol_LegendBg]     = vec(p.surfaceRaised, 0.92f);
    s.Colors[ImPlotCol_LegendBorder] = vec(p.border);
    s.Colors[ImPlotCol_LegendText]   = vec(p.text);
    s.Colors[ImPlotCol_TitleText]    = vec(p.text);
    s.Colors[ImPlotCol_InlayText]    = vec(p.text);
    s.Colors[ImPlotCol_AxisText]     = vec(p.textMuted);
    s.Colors[ImPlotCol_AxisGrid]     = vec(p.border, 0.55f);
    s.Colors[ImPlotCol_AxisTick]     = vec(p.border);
    s.Colors[ImPlotCol_Crosshairs]   = vec(p.textMuted);
    s.PlotPadding       = ImVec2(em * 0.5f, em * 0.5f);
    s.LabelPadding      = ImVec2(em * 0.3f, em * 0.25f);
    s.LegendPadding     = ImVec2(em * 0.5f, em * 0.4f);
    s.LegendInnerPadding = ImVec2(em * 0.4f, em * 0.25f);
    s.LegendSpacing     = ImVec2(em * 0.6f, em * 0.1f);
    s.PlotBorderSize    = 1.0f;
    s.MajorTickLen      = ImVec2(em * 0.3f, em * 0.3f);
    s.MinorTickLen      = ImVec2(em * 0.15f, em * 0.15f);
    s.MinorAlpha        = 0.35f;
    s.PlotMinSize       = ImVec2(em * 8.0f, em * 6.0f);
}

void TravelHistory::push(const double time, const float tenths) {
    if (!m_samples.empty() && time <= m_samples.back().time) return;
    m_samples.push_back({time, tenths});
    while (m_samples.size() > 2 && m_samples.front().time < time - 6.0) m_samples.pop_front();
}

std::vector<TravelSample> demoPattern(const double now, const double window) {
    // (time in the 4 s loop, depth in 0.1 mm)
    struct Frame {
        float t;
        float d;
    };
    static constexpr Frame FRAMES[] = {
        {0.00f, 0.0f},  {0.30f, 0.0f},  {0.55f, 30.0f}, {0.80f, 31.0f}, // press deep
        {0.95f, 26.0f}, {1.10f, 31.0f}, {1.25f, 26.5f}, {1.40f, 31.0f}, // flutter: Rapid Trigger territory
        {1.70f, 12.0f}, {1.95f, 12.5f}, {2.20f, 24.0f}, {2.45f, 24.0f}, // half release, press again
        {2.80f, 1.0f},  {3.05f, 0.0f},  {3.40f, 8.0f},  {3.70f, 0.0f},  // let go, light tap
        {4.00f, 0.0f},
    };
    constexpr double LOOP = 4.0;
    std::vector<TravelSample> samples;
    const double step = 1.0 / 240.0;
    for (double t = now - window; t <= now + 1e-9; t += step) {
        const double phase = std::fmod(std::fmod(t, LOOP) + LOOP, LOOP);
        const auto f = static_cast<float>(phase);
        float depth = 0.0f;
        for (std::size_t i = 1; i < std::size(FRAMES); ++i) {
            if (f <= FRAMES[i].t) {
                const float span = FRAMES[i].t - FRAMES[i - 1].t;
                depth = ease(FRAMES[i - 1].d, FRAMES[i].d, span > 0.0f ? (f - FRAMES[i - 1].t) / span : 1.0f);
                break;
            }
        }
        samples.push_back({t, depth});
    }
    return samples;
}

void travelPlot(const char* id, const std::span<const TravelSample> samples, const double now, const TravelPlotOptions& options,
                const ImVec2 size, const ThemePalette& p) {
    const auto& settings = options.settings;
    // Run the firmware's key logic over the samples to know when the key is "down".
    std::vector<double> xs;
    std::vector<double> depth;
    std::vector<double> top;
    std::vector<double> shade;
    std::vector<double> pressPoint;
    std::vector<double> releasePoint;
    xs.reserve(samples.size());
    KeyTriggerModel model;
    int presses = 0;
    bool wasDown = false;
    for (const TravelSample& sample : samples) {
        const auto travel = static_cast<std::uint8_t>(std::clamp(sample.tenths * TRAVEL_SCALE, 0.0f, static_cast<float>(FULL_TRAVEL)));
        const bool down = model.step(travel, settings);
        if (down && !wasDown && sample.time >= now - options.window) ++presses;
        wasDown = down;
        if (sample.time < now - options.window) continue;
        xs.push_back(sample.time - now);
        depth.push_back(sample.tenths / 10.0);
        top.push_back(-0.2);
        shade.push_back(down ? 4.2 : -0.2);
        if (settings.rapidTrigger && model.rapidPressPoint() > 0) {
            pressPoint.push_back(down ? NAN : static_cast<double>(model.rapidPressPoint()) / (TRAVEL_SCALE * 10.0));
            releasePoint.push_back(down ? static_cast<double>(model.rapidReleasePoint()) / (TRAVEL_SCALE * 10.0) : NAN);
        } else {
            pressPoint.push_back(NAN);
            releasePoint.push_back(NAN);
        }
    }

    if (!ImPlot::BeginPlot(id, size, PLOT_FLAGS)) return;
    ImPlot::SetupAxes(nullptr, "depth (mm)", AXIS_FLAGS | ImPlotAxisFlags_NoTickLabels, AXIS_FLAGS | ImPlotAxisFlags_Invert);
    ImPlot::SetupAxisLimits(ImAxis_X1, -options.window, 0.0, ImPlotCond_Always);
    ImPlot::SetupAxisLimits(ImAxis_Y1, -0.2, 4.2, ImPlotCond_Always);
    static constexpr double Y_TICKS[] = {0.0, 1.0, 2.0, 3.0, 4.0};
    ImPlot::SetupAxisTicks(ImAxis_Y1, Y_TICKS, 5);
    ImPlot::SetupLegend(ImPlotLocation_SouthWest, ImPlotLegendFlags_NoButtons | ImPlotLegendFlags_Horizontal);

    const int n = static_cast<int>(xs.size());
    if (n > 0) {
        ImPlotSpec shaded(ImPlotProp_FillColor, toImU32(p.accent), ImPlotProp_FillAlpha, 0.22f);
        ImPlot::PlotShaded("key down", xs.data(), top.data(), shade.data(), n, shaded);
    }

    const double actuation = settings.actuationTenths / 10.0;
    const double release = std::max(0, settings.actuationTenths - kbd::model::RELEASE_HYSTERESIS_TENTHS) / 10.0;
    ImPlot::PlotInfLines("actuation", &actuation, 1,
                         ImPlotSpec(ImPlotProp_LineColor, toImU32(p.accentText), ImPlotProp_LineWeight, 2.0f, ImPlotProp_Flags,
                                    static_cast<int>(ImPlotInfLinesFlags_Horizontal)));
    ImPlot::PlotInfLines("##release", &release, 1,
                         ImPlotSpec(ImPlotProp_LineColor, toImU32(p.accentText.withAlpha(110)), ImPlotProp_LineWeight, 1.0f,
                                    ImPlotProp_Flags, static_cast<int>(ImPlotInfLinesFlags_Horizontal) | static_cast<int>(ImPlotItemFlags_NoLegend)));
    if (options.dksShallowTenths || options.dksDeepTenths) {
        double lines[2];
        int count = 0;
        if (options.dksShallowTenths) lines[count++] = *options.dksShallowTenths / 10.0;
        if (options.dksDeepTenths) lines[count++] = *options.dksDeepTenths / 10.0;
        ImPlot::PlotInfLines("keystroke points", lines, count,
                             ImPlotSpec(ImPlotProp_LineColor, toImU32(p.warning), ImPlotProp_LineWeight, 1.5f, ImPlotProp_Flags,
                                        static_cast<int>(ImPlotInfLinesFlags_Horizontal)));
    }
    if (settings.rapidTrigger && n > 0) {
        ImPlot::PlotLine("re-press at", xs.data(), pressPoint.data(), n,
                         ImPlotSpec(ImPlotProp_LineColor, toImU32(p.success), ImPlotProp_LineWeight, 1.5f));
        ImPlot::PlotLine("release at", xs.data(), releasePoint.data(), n,
                         ImPlotSpec(ImPlotProp_LineColor, toImU32(p.warning), ImPlotProp_LineWeight, 1.5f));
    }
    if (n > 0) {
        ImPlot::PlotLine(options.live ? "key travel" : "example travel", xs.data(), depth.data(), n,
                         ImPlotSpec(ImPlotProp_LineColor, toImU32(p.text), ImPlotProp_LineWeight, 2.0f));
        ImPlot::Annotation(xs.back(), depth.back(), vec(p.surfaceRaised), ImVec2(-6, -10), true, "%.1f mm", depth.back());
    }
    ImPlot::TagY(actuation, vec(p.accent), "%.1f", actuation);
    char presses_text[48];
    std::snprintf(presses_text, sizeof(presses_text), "%d %s", presses, presses == 1 ? "press" : "presses");
    ImPlot::PlotText(presses_text, -options.window * 0.08, 0.25);
    ImPlot::EndPlot();
}

bool curvePlot(const char* id, kbd::ResponseCurve& curve, const std::optional<float> liveTenths, const std::optional<float> reported,
               const ImVec2 size, const ThemePalette& p) {
    bool changed = false;
    if (!ImPlot::BeginPlot(id, size, PLOT_FLAGS)) return false;
    ImPlot::SetupAxes("key travel (mm)", "stick (%)", AXIS_FLAGS, AXIS_FLAGS);
    ImPlot::SetupAxisLimits(ImAxis_X1, -0.1, 4.1, ImPlotCond_Always);
    ImPlot::SetupAxisLimits(ImAxis_Y1, -4.0, 104.0, ImPlotCond_Always);
    ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_NoButtons);

    // The curve exactly as the firmware evaluates it.
    std::vector<double> xs;
    std::vector<double> ys;
    for (int step = 0; step <= 160; ++step) {
        const float tenths = static_cast<float>(step) * 0.25f;
        xs.push_back(tenths / 10.0);
        ys.push_back(kbd::curveOutput(curve, tenths) / 127.0 * 100.0);
    }
    ImPlot::PlotShaded("##area", xs.data(), ys.data(), static_cast<int>(xs.size()), 0.0,
                       ImPlotSpec(ImPlotProp_FillColor, toImU32(p.accent), ImPlotProp_FillAlpha, 0.18f, ImPlotProp_Flags,
                                  static_cast<int>(ImPlotItemFlags_NoLegend)));
    ImPlot::PlotLine("response", xs.data(), ys.data(), static_cast<int>(xs.size()),
                     ImPlotSpec(ImPlotProp_LineColor, toImU32(p.accentText), ImPlotProp_LineWeight, 2.5f));
    const double linear[2] = {0.0, 4.0};
    const double linearY[2] = {0.0, 100.0};
    ImPlot::PlotLine("linear", linear, linearY, 2,
                     ImPlotSpec(ImPlotProp_LineColor, toImU32(p.textMuted.withAlpha(120)), ImPlotProp_LineWeight, 1.0f));

    if (liveTenths) {
        const double x = *liveTenths / 10.0;
        const double y = kbd::curveOutput(curve, *liveTenths) / 127.0 * 100.0;
        ImPlot::PlotInfLines("live key", &x, 1, ImPlotSpec(ImPlotProp_LineColor, toImU32(p.success), ImPlotProp_LineWeight, 1.5f));
        ImPlot::PlotScatter("##livept", &x, &y, 1,
                            ImPlotSpec(ImPlotProp_Marker, static_cast<int>(ImPlotMarker_Circle), ImPlotProp_MarkerSize, 6.0f,
                                       ImPlotProp_MarkerFillColor, toImU32(p.success), ImPlotProp_MarkerLineColor, toImU32(p.success),
                                       ImPlotProp_Flags, static_cast<int>(ImPlotItemFlags_NoLegend)));
        ImPlot::Annotation(x, y, vec(p.success), ImVec2(10, -10), true, "%.1f mm -> %.0f %%", x, y);
    }
    if (reported) {
        const double y = std::clamp(*reported, 0.0f, 1.0f) * 100.0;
        ImPlot::PlotInfLines("controller reports", &y, 1,
                             ImPlotSpec(ImPlotProp_LineColor, toImU32(Color{0x38, 0xBD, 0xF8}), ImPlotProp_LineWeight, 1.5f, ImPlotProp_Flags,
                                        static_cast<int>(ImPlotInfLinesFlags_Horizontal)));
    }

    // Draggable points; x is kept between the neighbours (the firmware needs an ordered curve).
    for (std::size_t i = 0; i < curve.size(); ++i) {
        double x = curve[i].x / 10.0;
        double y = curve[i].y / 127.0 * 100.0;
        bool hovered = false;
        bool held = false;
        if (ImPlot::DragPoint(static_cast<int>(i), &x, &y, vec(p.accentText), 7.0f, ImPlotDragToolFlags_Delayed, nullptr, &hovered, &held)) {
            const int low  = i == 0 ? 0 : curve[i - 1].x;
            const int high = i + 1 == curve.size() ? kbd::CURVE_MAX_X : curve[i + 1].x;
            curve[i].x = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(x * 10.0)), low, high));
            curve[i].y = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(y / 100.0 * 127.0)), 0, 127));
            changed = true;
        }
        if (hovered || held) {
            ImGui::SetTooltip("Point %zu: %.1f mm -> %.0f %%", i + 1, curve[i].x / 10.0, curve[i].y / 127.0 * 100.0);
        }
    }
    ImPlot::EndPlot();
    return changed;
}

} // namespace ui::plots
