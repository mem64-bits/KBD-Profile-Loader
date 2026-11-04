#include "kbd/GameController.h"

#include <algorithm>

namespace kbd {

bool isValidCurve(const ResponseCurve& curve) noexcept {
    for (std::size_t i = 0; i < curve.size(); ++i) {
        if (curve[i].x > CURVE_MAX_X || curve[i].y > CURVE_MAX_Y) return false;
        if (i > 0 && curve[i].x < curve[i - 1].x) return false;
    }
    return true;
}

float curveOutput(const ResponseCurve& curve, const float travelTenths) noexcept {
    if (travelTenths <= 0.0f) return 0.0f;

    const auto slope = [&](const std::size_t i) {
        const float dx = static_cast<float>(curve[i + 1].x) - static_cast<float>(curve[i].x);
        if (dx == 0.0f) return 0.0f;
        return (static_cast<float>(curve[i + 1].y) - static_cast<float>(curve[i].y)) / dx;
    };

    float value = 0.0f;
    if (travelTenths >= static_cast<float>(curve[3].x)) {
        value = static_cast<float>(curve[3].y);
    } else {
        for (std::size_t i = 3; i-- > 0;) {
            if (travelTenths >= static_cast<float>(curve[i].x)) {
                value = static_cast<float>(curve[i].y) + slope(i) * (travelTenths - static_cast<float>(curve[i].x));
                break;
            }
        }
    }
    return std::clamp(value, 0.0f, static_cast<float>(CURVE_MAX_Y));
}

} // namespace kbd
