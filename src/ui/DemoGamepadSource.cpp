#include "ui/DemoGamepadSource.h"

#include <array>
#include <chrono>
#include <cmath>
#include <numbers>

namespace ui {

namespace {

    using namespace gamepad;

    constexpr std::uint64_t FRAME_NS  = 1'000'000'000ULL / 60;
    constexpr std::uint64_t SAMPLE_NS = 8'000'000ULL; // the demo "reports" at 125 Hz

    enum : std::uint8_t { NONE = 0, X_ = 1 << 0, Y_ = 1 << 1, A_ = 1 << 2, B_ = 1 << 3, RT_ = 1 << 4, LB_ = 1 << 5 };

    struct Step {
        std::uint8_t direction; // numpad notation
        std::uint8_t buttons;
        std::uint8_t frames;
    };

    // Fireball (236+X), dash and overhead (66, 6+Y), crouch block into sweep (4, 1, 2+A+B), jump-in with RT.
    constexpr std::array<Step, 22> SCRIPT{{
        {5, NONE, 40}, {2, NONE, 4}, {3, NONE, 3}, {6, X_, 3}, {6, NONE, 3},
        {5, NONE, 28}, {6, NONE, 3}, {5, NONE, 3}, {6, NONE, 4}, {6, Y_, 3},
        {5, NONE, 24}, {4, NONE, 14}, {1, NONE, 6}, {2, A_ | B_, 3}, {2, NONE, 6},
        {5, NONE, 24}, {8, NONE, 4}, {9, NONE, 10}, {6, RT_, 8}, {6, NONE, 4},
        {5, LB_, 6}, {5, NONE, 36},
    }};

    constexpr std::uint64_t scriptFrames() noexcept {
        std::uint64_t total = 0;
        for (const Step& step : SCRIPT) total += step.frames;
        return total;
    }

    std::uint64_t steadyNs() {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }

} // namespace

DemoGamepadSource::DemoGamepadSource() : m_startNs(steadyNs()), m_lastSampleNs(m_startNs) {}

std::vector<PadInfo> DemoGamepadSource::pads() const {
    return {PadInfo{1, "Keychron K2 HE (demo)", KEYCHRON_VENDOR_ID, 0x0E21, PadFamily::Xbox}};
}

std::optional<std::uint32_t> DemoGamepadSource::selectedPad() const {
    return 1u;
}

void DemoGamepadSource::selectPad(std::uint32_t) {}

PadState DemoGamepadSource::stateAt(const std::uint64_t elapsedNs) noexcept {
    PadState state;

    // Left stick + buttons from the script (digital, like WASD keys driving the stick).
    std::uint64_t frame = (elapsedNs / FRAME_NS) % scriptFrames();
    const Step* current = &SCRIPT.front();
    for (const Step& step : SCRIPT) {
        if (frame < step.frames) {
            current = &step;
            break;
        }
        frame -= step.frames;
    }
    const int dx = (current->direction - 1) % 3 - 1;
    const int dy = (current->direction - 1) / 3 - 1;
    state.set(Axis::LeftX, static_cast<float>(dx));
    state.set(Axis::LeftY, static_cast<float>(dy));
    state.set(Button::West, (current->buttons & X_) != 0);
    state.set(Button::North, (current->buttons & Y_) != 0);
    state.set(Button::South, (current->buttons & A_) != 0);
    state.set(Button::East, (current->buttons & B_) != 0);
    state.set(Button::LeftShoulder, (current->buttons & LB_) != 0);

    // Right stick: a slow circle; triggers: analog presses like a key going down and up.
    const double seconds = static_cast<double>(elapsedNs) / 1e9;
    const double angle   = seconds * 2.0 * std::numbers::pi / 3.2;
    state.set(Axis::RightX, static_cast<float>(0.85 * std::cos(angle)));
    state.set(Axis::RightY, static_cast<float>(0.85 * std::sin(angle)));
    const double press = std::max(0.0, std::sin(seconds * 2.0 * std::numbers::pi / 4.0));
    state.set(Axis::LeftTrigger, static_cast<float>(0.45 * press * press)); // a half press: analog, never a "button"
    state.set(Axis::RightTrigger, (current->buttons & RT_) != 0 ? 1.0f : 0.0f);
    return state;
}

PadState DemoGamepadSource::current() const {
    return stateAt(nowNs() - m_startNs);
}

std::vector<Sample> DemoGamepadSource::takeSamples() {
    std::vector<Sample> samples;
    const std::uint64_t now = nowNs();
    // At most two seconds of catch-up (the page may have been closed for a while).
    if (now - m_lastSampleNs > 2'000'000'000ULL) m_lastSampleNs = now - 2'000'000'000ULL;
    for (std::uint64_t t = m_lastSampleNs + SAMPLE_NS; t <= now; t += SAMPLE_NS) {
        samples.push_back({t, stateAt(t - m_startNs)});
        m_lastSampleNs = t;
    }
    return samples;
}

std::uint64_t DemoGamepadSource::nowNs() const {
    return steadyNs();
}

std::string DemoGamepadSource::backendName() const {
    return "Demo controller";
}

void DemoGamepadSource::setActive(bool) {}

} // namespace ui
