#include "kbd/FirmwareModel.h"

#include <algorithm>
#include <cmath>

namespace kbd::model {

namespace {

    // GC_AXIS_X .. GC_AXIS_RZ; target 2*i is the negative half of axis i, 2*i+1 the positive half.
    constexpr int AXIS_X  = 0;
    constexpr int AXIS_Y  = 1;
    constexpr int AXIS_Z  = 2; // triggers
    constexpr int AXIS_RX = 3;
    constexpr int AXIS_RY = 4;
    constexpr int AXIS_COUNT = 6;

    constexpr std::uint32_t MAX_VALUES[12] = {0x8000, 0x7FFF, 0x8000, 0x7FFF, 0xFF, 0xFF, 0x8000, 0x7FFF, 0x8000, 0x7FFF, 0x8000, 0x7FFF};

    // sqrt_uint32 (sqrt.c): the integer square root, rounded down.
    std::uint32_t isqrt(const std::uint32_t value) noexcept {
        std::uint32_t root = static_cast<std::uint32_t>(std::sqrt(static_cast<double>(value)));
        while (static_cast<std::uint64_t>(root) * root > value) --root;
        while (static_cast<std::uint64_t>(root + 1) * (root + 1) <= value) ++root;
        return root;
    }

    // travel_to_xinput_value
    std::uint32_t xinputValue(const int axis, const std::uint8_t travel, const ResponseCurve& curve) noexcept {
        float slope[3];
        for (int i = 0; i < 3; ++i) {
            // game_controller_set_curve keeps the old slope when two points share an x; 0 is close enough here.
            const int dx = curve[static_cast<std::size_t>(i + 1)].x - curve[static_cast<std::size_t>(i)].x;
            slope[i] = dx == 0 ? 0.0f
                               : static_cast<float>(curve[static_cast<std::size_t>(i + 1)].y - curve[static_cast<std::size_t>(i)].y) /
                                     static_cast<float>(dx);
        }
        std::uint32_t value = 0;
        if (axis == 4 || axis == 5) {
            value = static_cast<std::uint32_t>(travel) * 127u / 40u / TRAVEL_SCALE; // triggers ignore the curve
        } else if (travel == 0) {
            value = 0;
        } else {
            const auto scaled = [&](std::size_t i) { return static_cast<int>(curve[i].x) * TRAVEL_SCALE; };
            const auto along  = [&](std::size_t i) {
                return static_cast<std::uint32_t>(static_cast<float>(curve[i].y) +
                                                  slope[i] * static_cast<float>(travel - scaled(i)) / TRAVEL_SCALE);
            };
            if (travel >= scaled(3))      value = curve[3].y;
            else if (travel >= scaled(2)) value = along(2);
            else if (travel >= scaled(1)) value = along(1);
            else if (travel >= scaled(0)) value = along(0);
        }
        const std::uint32_t range = MAX_VALUES[axis];
        value = value * range / 127u;
        if (value > range) value = range;
        return value & 0xFFFFu;
    }

    void clampStick(std::int32_t& x, std::int32_t& y, const std::int8_t dirX, const std::int8_t dirY) noexcept {
        const auto sx = static_cast<std::uint32_t>(x >> 8);
        const auto sy = static_cast<std::uint32_t>(y >> 8);
        const std::uint32_t square = sx * sx + sy * sy; // wraps like the firmware's uint32 maths: same result
        if (square <= 0x80u * 0x80u) return;
        const float ratio = 140.0f / static_cast<float>(isqrt(square));
        x = static_cast<std::int32_t>(static_cast<float>(x) * ratio);
        y = static_cast<std::int32_t>(static_cast<float>(y) * ratio);
        if (dirX < 0) { if (x < -0x8000) x = -0x8000; } else if (x > 0x7FFF) x = 0x7FFF;
        if (dirY < 0) { if (y < -0x8000) y = -0x8000; } else if (y > 0x7FFF) y = 0x7FFF;
    }

    std::int16_t toShort(const std::int32_t value) noexcept {
        // xinput.x is int16_t: the C assignment keeps the low 16 bits.
        return static_cast<std::int16_t>(static_cast<std::uint16_t>(static_cast<std::uint32_t>(value) & 0xFFFFu));
    }

} // namespace

void KeyTriggerModel::reset() noexcept {
    m_state = State::RegularReleased;
    m_rapidPress = m_rapidRelease = 0;
}

bool KeyTriggerModel::step(const std::uint8_t travel, const TriggerSettings& settings) {
    const int act     = settings.actuationTenths * TRAVEL_SCALE;
    const int deact   = std::max(0, settings.actuationTenths - RELEASE_HYSTERESIS_TENTHS) * TRAVEL_SCALE;
    const int t       = travel;

    if (!settings.rapidTrigger) {
        // regular_trigger_action
        if (pressed() && (t == 0 || t < deact)) m_state = State::RegularReleased;
        else if (!pressed() && t >= act) m_state = State::RegularPressed;
        return pressed();
    }

    const int sense   = settings.pressTenths * TRAVEL_SCALE;
    const int release = settings.releaseTenths * TRAVEL_SCALE;
    const int bottom  = BOTTOM_DEAD_ZONE_TENTHS * TRAVEL_SCALE;
    int update = 0;
    switch (m_state) {
        case State::RegularReleased:
            if (t >= act) {
                m_state = State::RegularPressed;
                update  = 1;
            }
            break;
        case State::RegularPressed:
        case State::RapidPressed:
            if (m_state == State::RapidPressed && t > FULL_TRAVEL) break;
            if (t <= deact) {
                m_state = State::RegularReleased;
            } else if (t <= m_rapidRelease && t < bottom - release) {
                m_state = State::RapidReleased;
                update  = -1;
            } else if (t > m_rapidPress) {
                update = 1;
            }
            break;
        case State::RapidReleased:
            if (t <= deact) {
                m_state = State::RegularReleased;
            } else if (t >= m_rapidPress && t >= act) {
                m_state = State::RapidPressed;
                update  = 1;
            } else if (t < m_rapidRelease) {
                update = -1;
            }
            break;
    }
    if (update > 0) {
        m_rapidRelease = static_cast<std::uint8_t>(std::max(0, t - release));
        m_rapidPress   = static_cast<std::uint8_t>(t);
    } else if (update < 0) {
        m_rapidRelease = static_cast<std::uint8_t>(t);
        m_rapidPress   = static_cast<std::uint8_t>(std::min(255, t + sense));
    }
    return pressed();
}

std::uint16_t xinputButtonBit(const std::uint8_t target) noexcept {
    static constexpr std::uint8_t MAP[16] = {12, 13, 14, 15, 8, 9, 5, 4, 6, 7, 0, 1, 2, 3, 10, 11};
    if (target < GamepadTarget::BUTTON_BASE || target >= GamepadTarget::BUTTON_BASE + 16) return 0;
    return static_cast<std::uint16_t>(1u << MAP[target - GamepadTarget::BUTTON_BASE]);
}

void XInputModel::reset() noexcept {
    m_travel = {};
    m_dir    = {};
    m_value  = {};
    m_buttons = 0;
}

XInputReport XInputModel::step(const std::span<const GamepadKey> keys, const ResponseCurve& curve) {
    // xinput_update, per key
    for (const GamepadKey& key : keys) {
        const std::uint8_t travel = key.travel < TRAVEL_SCALE ? std::uint8_t{0} : key.travel;
        if (key.target < 12) m_travel[key.target] = travel;
        if (const std::uint16_t bit = xinputButtonBit(key.target)) {
            if (key.pressed) m_buttons = static_cast<std::uint16_t>(m_buttons | bit);
            else             m_buttons = static_cast<std::uint16_t>(m_buttons & ~bit);
        }
    }

    // xinput_action
    XInputReport report;
    constexpr int DIRECT = 2 * TRAVEL_SCALE;
    for (int i = 0; i < AXIS_COUNT; ++i) {
        const int neg = i * 2;
        const int pos = i * 2 + 1;
        const std::uint8_t tn = m_travel[static_cast<std::size_t>(neg)];
        const std::uint8_t tp = m_travel[static_cast<std::size_t>(pos)];
        if (i == AXIS_Z) {
            report.leftTrigger  = static_cast<std::uint8_t>(xinputValue(neg, tn, curve));
            report.rightTrigger = static_cast<std::uint8_t>(xinputValue(pos, tp, curve));
            continue;
        }
        std::int8_t& dir = m_dir[static_cast<std::size_t>(i)];
        if (tn > TRAVEL_SCALE && tp > TRAVEL_SCALE) {
            if (tn > tp + DIRECT)      dir = -1;
            else if (tn + DIRECT < tp) dir = 1;
        } else if (tn) {
            dir = -1;
        } else if (tp) {
            dir = 1;
        } else {
            dir = 0;
        }
        m_value[static_cast<std::size_t>(i)] = dir < 0 ? -static_cast<std::int32_t>(xinputValue(neg, tn, curve))
                                                       : static_cast<std::int32_t>(xinputValue(pos, tp, curve));
    }

    clampStick(m_value[AXIS_X], m_value[AXIS_Y], m_dir[AXIS_X], m_dir[AXIS_Y]);
    clampStick(m_value[AXIS_RX], m_value[AXIS_RY], m_dir[AXIS_RX], m_dir[AXIS_RY]);

    report.leftX   = toShort(m_value[AXIS_X]);
    report.leftY   = toShort(m_value[AXIS_Y]);
    report.rightX  = toShort(m_value[AXIS_RX]);
    report.rightY  = toShort(m_value[AXIS_RY]);
    report.buttons = m_buttons;
    return report;
}

// ---------------------------------------------------------------------------------------------------------------

void SocdPairModel::reset() noexcept {
    m_state = 0;
    m_previous = {};
    m_previousDown1 = m_previousDown2 = false;
}

SocdOutput SocdPairModel::step(const std::uint8_t type, const SocdKey key1, const SocdKey key2) {
    constexpr std::uint8_t KEY1 = 1;
    constexpr std::uint8_t KEY2 = 2;

    const bool changed1 = key1.down != m_previousDown1;
    const bool changed2 = key2.down != m_previousDown2;
    bool mask1 = true; // key may be sent
    bool mask2 = true;

    if (type != SocdType::NONE) {
        bool keepLast = false;
        if (key1.down && key2.down) {
            switch (type) {
                case SocdType::DEEPER_TRAVEL:
                case SocdType::DEEPER_TRAVEL_SINGLE:
                    if (key1.travel > SOCD_BOTTOM_OUT && key2.travel > SOCD_BOTTOM_OUT) {
                        if (type == SocdType::DEEPER_TRAVEL_SINGLE) keepLast = true;
                    } else if (m_state == KEY2 && key1.travel > key2.travel) {
                        mask2 = false;
                    } else if (m_state == KEY1 && key1.travel < key2.travel) {
                        mask1 = false;
                    } else {
                        keepLast = true;
                    }
                    break;
                case SocdType::LAST_KEYSTROKE:
                    if (m_previous.key1 && key2.down && changed2) {
                        mask1   = false;
                        m_state = KEY2;
                    } else if (m_previous.key2 && key1.down && changed1) {
                        mask2   = false;
                        m_state = KEY1;
                    } else {
                        keepLast = true;
                    }
                    break;
                case SocdType::KEY_1:
                    mask2   = false;
                    m_state = KEY1;
                    break;
                case SocdType::KEY_2:
                    mask1   = false;
                    m_state = KEY2;
                    break;
                case SocdType::NEUTRAL:
                    mask1 = mask2 = false;
                    m_state = 0;
                    break;
                default:
                    break;
            }
        } else if (key1.down) {
            m_state = KEY1;
        } else if (key2.down) {
            m_state = KEY2;
        }

        if (keepLast) {
            if (m_state == KEY1)      mask2 = false;
            else if (m_state == KEY2) mask1 = false;
        }
    }

    m_previousDown1 = key1.down;
    m_previousDown2 = key2.down;
    m_previous = SocdOutput{key1.down && mask1, key2.down && mask2};
    return m_previous;
}

} // namespace kbd::model
