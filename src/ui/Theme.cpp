#include "ui/Theme.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace ui {

namespace {

    struct ColorField {
        const char*         key;
        Color ThemePalette::* member;
    };

    struct MetricField {
        const char*         key;
        float ThemeMetrics::* member;
        float               minimum;
        float               maximum;
    };

    constexpr std::array<ColorField, 14> COLOR_FIELDS{{
        {"background",    &ThemePalette::background},
        {"surface",       &ThemePalette::surface},
        {"surfaceRaised", &ThemePalette::surfaceRaised},
        {"border",        &ThemePalette::border},
        {"text",          &ThemePalette::text},
        {"textMuted",     &ThemePalette::textMuted},
        {"textOnAccent",  &ThemePalette::textOnAccent},
        {"accent",        &ThemePalette::accent},
        {"accentHover",   &ThemePalette::accentHover},
        {"accentActive",  &ThemePalette::accentActive},
        {"accentText",    &ThemePalette::accentText},
        {"success",       &ThemePalette::success},
        {"warning",       &ThemePalette::warning},
        {"danger",        &ThemePalette::danger},
    }};

    constexpr std::array<MetricField, 14> METRIC_FIELDS{{
        {"windowRounding",   &ThemeMetrics::windowRounding,   0.0f, 32.0f},
        {"frameRounding",    &ThemeMetrics::frameRounding,    0.0f, 32.0f},
        {"popupRounding",    &ThemeMetrics::popupRounding,    0.0f, 32.0f},
        {"tabRounding",      &ThemeMetrics::tabRounding,      0.0f, 32.0f},
        {"scrollbarRounding",&ThemeMetrics::scrollbarRounding,0.0f, 32.0f},
        {"windowBorderSize", &ThemeMetrics::windowBorderSize, 0.0f, 4.0f},
        {"frameBorderSize",  &ThemeMetrics::frameBorderSize,  0.0f, 4.0f},
        {"windowPaddingX",   &ThemeMetrics::windowPaddingX,   0.0f, 48.0f},
        {"windowPaddingY",   &ThemeMetrics::windowPaddingY,   0.0f, 48.0f},
        {"framePaddingX",    &ThemeMetrics::framePaddingX,    0.0f, 32.0f},
        {"framePaddingY",    &ThemeMetrics::framePaddingY,    0.0f, 32.0f},
        {"itemSpacingX",     &ThemeMetrics::itemSpacingX,     0.0f, 32.0f},
        {"itemSpacingY",     &ThemeMetrics::itemSpacingY,     0.0f, 32.0f},
        {"fontSize",         &ThemeMetrics::fontSize,         10.0f, 32.0f},
    }};

    void report(std::vector<ThemeIssue>* issues, std::string field, std::string message) {
        if (issues != nullptr) issues->push_back({std::move(field), std::move(message)});
    }

    bool isKnownTopLevelKey(const std::string& key) {
        return key == "name" || key == "fontFile" || key == "colors" || key == "metrics";
    }

} // namespace

Theme Theme::blackPurple() {
    return Theme{};
}

std::optional<Theme> Theme::fromJson(const nlohmann::json& json, std::vector<ThemeIssue>* issues) {
    if (!json.is_object()) {
        report(issues, "", "A theme must be a JSON object.");
        return std::nullopt;
    }

    Theme theme = Theme::blackPurple();

    for (const auto& [key, value] : json.items()) {
        (void)value;
        if (!isKnownTopLevelKey(key)) report(issues, key, "Unknown key ignored.");
    }

    if (const auto it = json.find("name"); it != json.end()) {
        if (it->is_string()) {
            theme.name = it->get<std::string>();
        } else {
            report(issues, "name", "Expected a string.");
        }
    }

    if (const auto it = json.find("fontFile"); it != json.end()) {
        if (it->is_string()) {
            theme.fontFile = it->get<std::string>();
        } else {
            report(issues, "fontFile", "Expected a path to a .ttf file as a string.");
        }
    }

    bool accentGiven = false;
    bool hoverGiven  = false;
    bool activeGiven = false;

    if (const auto it = json.find("colors"); it != json.end()) {
        if (!it->is_object()) {
            report(issues, "colors", "Expected an object of colour names to \"#RRGGBB\" strings.");
        } else {
            for (const auto& [key, value] : it->items()) {
                const auto field = std::find_if(COLOR_FIELDS.begin(), COLOR_FIELDS.end(),
                                                [&key](const ColorField& f) { return key == f.key; });
                if (field == COLOR_FIELDS.end()) {
                    report(issues, "colors." + key, "Unknown colour name ignored.");
                    continue;
                }
                if (!value.is_string()) {
                    report(issues, "colors." + key, "Expected a string such as \"#7C3AED\".");
                    continue;
                }
                const auto parsed = Color::fromHex(value.get<std::string>());
                if (!parsed) {
                    report(issues, "colors." + key, "Invalid colour '" + value.get<std::string>() + "'.");
                    continue;
                }

                theme.palette.*(field->member) = *parsed;
                accentGiven = accentGiven || key == "accent";
                hoverGiven  = hoverGiven  || key == "accentHover";
                activeGiven = activeGiven || key == "accentActive";
            }
        }
    }

    // Convenience: a theme that only sets "accent" still gets coherent hover/pressed shades.
    if (accentGiven && !hoverGiven)  theme.palette.accentHover  = theme.palette.accent.lighten(0.18f);
    if (accentGiven && !activeGiven) theme.palette.accentActive = theme.palette.accent.darken(0.18f);

    if (const auto it = json.find("metrics"); it != json.end()) {
        if (!it->is_object()) {
            report(issues, "metrics", "Expected an object of sizes.");
        } else {
            for (const auto& [key, value] : it->items()) {
                const auto field = std::find_if(METRIC_FIELDS.begin(), METRIC_FIELDS.end(),
                                                [&key](const MetricField& f) { return key == f.key; });
                if (field == METRIC_FIELDS.end()) {
                    report(issues, "metrics." + key, "Unknown metric ignored.");
                    continue;
                }
                if (!value.is_number()) {
                    report(issues, "metrics." + key, "Expected a number.");
                    continue;
                }

                const float requested = value.get<float>();
                const float clamped   = std::clamp(requested, field->minimum, field->maximum);
                if (!std::isfinite(requested) || clamped != requested) {
                    report(issues, "metrics." + key, "Value out of range; clamped.");
                }
                theme.metrics.*(field->member) = std::isfinite(clamped) ? clamped : field->minimum;
            }
        }
    }

    return theme;
}

nlohmann::json Theme::toJson() const {
    nlohmann::json colors = nlohmann::json::object();
    for (const auto& field : COLOR_FIELDS) {
        const Color& color = palette.*(field.member);
        colors[field.key] = color.toHex(color.a != 255);
    }

    nlohmann::json sizes = nlohmann::json::object();
    for (const auto& field : METRIC_FIELDS) {
        sizes[field.key] = metrics.*(field.member);
    }

    nlohmann::json json = nlohmann::json::object();
    json["name"]     = name;
    json["fontFile"] = fontFile;
    json["colors"]   = std::move(colors);
    json["metrics"] = std::move(sizes);
    return json;
}

} // namespace ui
