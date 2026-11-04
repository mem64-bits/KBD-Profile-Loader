#include "ui/GamepadModel.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ui::gamepad {

namespace {

    constexpr std::array<const char*, BUTTON_COUNT> XBOX_BUTTONS{
        "A", "B", "X", "Y", "View", "Guide", "Menu", "LS click", "RS click", "LB", "RB", "D-pad up", "D-pad down", "D-pad left",
        "D-pad right"};
    constexpr std::array<const char*, BUTTON_COUNT> PLAYSTATION_BUTTONS{
        "Cross", "Circle", "Square", "Triangle", "Create", "Home", "Options", "L3", "R3", "L1", "R1",
        "D-pad up", "D-pad down", "D-pad left", "D-pad right"};

    // Numpad digit for a direction given as -1 / 0 / +1 steps (+y = up).
    int numpadFromSteps(const int dx, const int dy) noexcept {
        static constexpr int TABLE[3][3] = {
            {1, 2, 3}, // down row
            {4, 5, 6},
            {7, 8, 9}, // up row
        };
        return TABLE[dy + 1][dx + 1];
    }

} // namespace

const char* buttonName(const Button button, const LabelStyle style) noexcept {
    const auto index = static_cast<std::size_t>(button);
    if (index >= BUTTON_COUNT) return "?";
    return style == LabelStyle::PlayStation ? PLAYSTATION_BUTTONS[index] : XBOX_BUTTONS[index];
}

const char* axisName(const Axis axis) noexcept {
    switch (axis) {
        case Axis::LeftX:        return "Left stick X";
        case Axis::LeftY:        return "Left stick Y";
        case Axis::RightX:       return "Right stick X";
        case Axis::RightY:       return "Right stick Y";
        case Axis::LeftTrigger:  return "Left trigger";
        case Axis::RightTrigger: return "Right trigger";
        case Axis::Count:        break;
    }
    return "?";
}

const char* familyName(const PadFamily family) noexcept {
    switch (family) {
        case PadFamily::Xbox:        return "Xbox-style (XInput)";
        case PadFamily::PlayStation: return "PlayStation-style";
        case PadFamily::Nintendo:    return "Nintendo-style";
        case PadFamily::Generic:     break;
    }
    return "Generic controller";
}

LabelStyle defaultLabels(const PadFamily family) noexcept {
    return family == PadFamily::PlayStation ? LabelStyle::PlayStation : LabelStyle::Xbox;
}

int numpadDirection(const PadState& state, const float threshold) noexcept {
    const int padX = (state.pressed(Button::DpadRight) ? 1 : 0) - (state.pressed(Button::DpadLeft) ? 1 : 0);
    const int padY = (state.pressed(Button::DpadUp) ? 1 : 0) - (state.pressed(Button::DpadDown) ? 1 : 0);
    if (padX != 0 || padY != 0) return numpadFromSteps(padX, padY);

    const float x = state.axis(Axis::LeftX);
    const float y = state.axis(Axis::LeftY);
    if (std::hypot(x, y) < threshold) return 5;

    // Eight 45-degree sectors centred on the axes and the diagonals.
    const double angle  = std::atan2(static_cast<double>(y), static_cast<double>(x));
    const int    sector = static_cast<int>(std::lround(angle / (std::numbers::pi / 4.0))) & 7; // 0 = right, counter-clockwise
    static constexpr int SECTOR_TO_NUMPAD[8] = {6, 9, 8, 7, 4, 1, 2, 3};
    return SECTOR_TO_NUMPAD[sector];
}

AttackSet attacksOf(const PadState& state) noexcept {
    AttackSet set{};
    set[static_cast<std::size_t>(Attack::South)]         = state.pressed(Button::South);
    set[static_cast<std::size_t>(Attack::East)]          = state.pressed(Button::East);
    set[static_cast<std::size_t>(Attack::West)]          = state.pressed(Button::West);
    set[static_cast<std::size_t>(Attack::North)]         = state.pressed(Button::North);
    set[static_cast<std::size_t>(Attack::LeftShoulder)]  = state.pressed(Button::LeftShoulder);
    set[static_cast<std::size_t>(Attack::RightShoulder)] = state.pressed(Button::RightShoulder);
    set[static_cast<std::size_t>(Attack::LeftTrigger)]   = state.axis(Axis::LeftTrigger) >= TRIGGER_PRESS_THRESHOLD;
    set[static_cast<std::size_t>(Attack::RightTrigger)]  = state.axis(Axis::RightTrigger) >= TRIGGER_PRESS_THRESHOLD;
    return set;
}

const char* attackLabel(const Attack attack, const Notation notation, const LabelStyle style) noexcept {
    const bool ps = style == LabelStyle::PlayStation;
    switch (notation) {
        case Notation::Tekken:
            switch (attack) {
                case Attack::West:  return "1";
                case Attack::North: return "2";
                case Attack::South: return "3";
                case Attack::East:  return "4";
                default:            break;
            }
            break;
        case Notation::StreetFighter:
            switch (attack) {
                case Attack::West:          return "LP";
                case Attack::North:         return "MP";
                case Attack::RightShoulder: return "HP";
                case Attack::South:         return "LK";
                case Attack::East:          return "MK";
                case Attack::RightTrigger:  return "HK";
                default:                    break;
            }
            break;
        case Notation::PadLabels:
            break;
    }
    switch (attack) {
        case Attack::South:         return ps ? "Cross" : "A";
        case Attack::East:          return ps ? "Circle" : "B";
        case Attack::West:          return ps ? "Square" : "X";
        case Attack::North:         return ps ? "Triangle" : "Y";
        case Attack::LeftShoulder:  return ps ? "L1" : "LB";
        case Attack::RightShoulder: return ps ? "R1" : "RB";
        case Attack::LeftTrigger:   return ps ? "L2" : "LT";
        case Attack::RightTrigger:  return ps ? "R2" : "RT";
        case Attack::Count:         break;
    }
    return nullptr;
}

bool HistoryEntry::anyPressed() const noexcept {
    return std::any_of(pressed.begin(), pressed.end(), [](const bool b) { return b; });
}

bool HistoryEntry::anyHeld() const noexcept {
    return std::any_of(held.begin(), held.end(), [](const bool b) { return b; });
}

InputHistory::InputHistory(const std::size_t capacity, const double framesPerSecond)
    : m_capacity(std::max<std::size_t>(capacity, 1)), m_fps(framesPerSecond > 0.0 ? framesPerSecond : 60.0) {}

void InputHistory::push(const Sample& sample) {
    const int       direction = numpadDirection(sample.state);
    const AttackSet held      = attacksOf(sample.state);

    if (m_started && !m_entries.empty()) {
        HistoryEntry& current = m_entries.front();
        if (current.direction == direction && current.held == held) return; // axis jitter inside the same input
        current.endNs = std::max(sample.timestampNs, current.startNs);
    }

    HistoryEntry entry;
    entry.direction = direction;
    entry.held      = held;
    entry.startNs   = sample.timestampNs;
    const AttackSet before = m_entries.empty() ? AttackSet{} : m_entries.front().held;
    for (std::size_t i = 0; i < ATTACK_COUNT; ++i) entry.pressed[i] = held[i] && !before[i];

    m_entries.push_front(entry);
    m_started = true;
    while (m_entries.size() > m_capacity) m_entries.pop_back();
}

void InputHistory::clear() {
    m_entries.clear();
    m_started = false;
}

std::uint32_t InputHistory::frames(const HistoryEntry& entry, const std::uint64_t nowNs) const noexcept {
    const std::uint64_t end = entry.endNs != 0 ? entry.endNs : std::max(nowNs, entry.startNs);
    const double seconds = static_cast<double>(end - entry.startNs) / 1e9;
    const auto counted = static_cast<std::uint32_t>(std::llround(seconds * m_fps));
    return std::max<std::uint32_t>(counted, 1);
}

void InputHistory::setFramesPerSecond(const double fps) noexcept {
    if (fps > 0.0) m_fps = fps;
}

StickTrail::StickTrail(const std::uint64_t lengthNs, const std::size_t capacity)
    : m_lengthNs(lengthNs), m_capacity(std::max<std::size_t>(capacity, 2)) {}

void StickTrail::push(const float x, const float y, const std::uint64_t timestampNs) {
    m_points.push_back({x, y, timestampNs});
    while (m_points.size() > m_capacity) m_points.pop_front();
}

void StickTrail::expire(const std::uint64_t nowNs) {
    while (!m_points.empty() && nowNs > m_points.front().timestampNs && nowNs - m_points.front().timestampNs > m_lengthNs) {
        m_points.pop_front();
    }
}

void CircularityTest::add(const float x, const float y) noexcept {
    const float radius = std::hypot(x, y);
    if (radius < MIN_RADIUS) return;
    const double turn = (std::atan2(static_cast<double>(y), static_cast<double>(x)) + std::numbers::pi) / (2.0 * std::numbers::pi);
    const auto bucket = static_cast<std::size_t>(std::floor(turn * static_cast<double>(BUCKETS))) % BUCKETS;
    m_radii[bucket] = std::max(m_radii[bucket], radius);
}

void CircularityTest::reset() noexcept {
    m_radii.fill(0.0f);
}

float CircularityTest::coverage() const noexcept {
    const auto reached = std::count_if(m_radii.begin(), m_radii.end(), [](const float r) { return r > 0.0f; });
    return static_cast<float>(reached) / static_cast<float>(BUCKETS);
}

std::optional<float> CircularityTest::averageErrorPercent() const noexcept {
    double      sum = 0.0;
    std::size_t reached = 0;
    for (const float r : m_radii) {
        if (r <= 0.0f) continue;
        sum += std::abs(static_cast<double>(r) - 1.0);
        ++reached;
    }
    if (reached == 0) return std::nullopt;
    return static_cast<float>(sum / static_cast<double>(reached) * 100.0);
}

float CircularityTest::maxRadius() const noexcept {
    return *std::max_element(m_radii.begin(), m_radii.end());
}

} // namespace ui::gamepad
