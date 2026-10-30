#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "ui/imgui/SettingsPanel.h"
#include "ui/ThemeManager.h"
#include "ui/imgui/ImGuiTheme.h"
#include "ui/imgui/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <vector>

#ifndef KBD_VERSION
    #define KBD_VERSION "dev"
#endif

namespace ui {

namespace {

    void mutedWrapped(const ThemePalette& palette, const char* text) {
        ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(palette.textMuted));
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    }

    std::uint8_t toByte(const float unit) {
        return static_cast<std::uint8_t>(std::lround(std::clamp(unit, 0.0f, 1.0f) * 255.0f));
    }

    // Colour swatch + label; opens a picker when clicked. Alpha is kept as it was.
    bool colorRow(const char* label, Color& color) {
        float rgb[3] = {static_cast<float>(color.r) / 255.0f, static_cast<float>(color.g) / 255.0f, static_cast<float>(color.b) / 255.0f};
        if (!ImGui::ColorEdit3(label, rgb, ImGuiColorEditFlags_NoInputs)) return false;
        color = Color(toByte(rgb[0]), toByte(rgb[1]), toByte(rgb[2]), color.a);
        return true;
    }

} // namespace

SettingsPanel::SettingsPanel(ISystemIntegration& system, std::filesystem::path themeFile)
    : m_system(system), m_themeFile(std::move(themeFile))
{}

SettingsPanel::~SettingsPanel() = default;

const char* SettingsPanel::title() const noexcept {
    return "Settings";
}

void SettingsPanel::render(const AppSnapshot& state, const Theme& theme) {
    const ThemePalette& p = theme.palette;

    drawThemeSection(theme);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    drawSystemSection(p);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    drawDeviceSection(state, p);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    drawAboutSection(p);
}

void SettingsPanel::drawThemeSection(const Theme& theme) {
    const ThemePalette& p = theme.palette;
    widgets::sectionLabel("THEME", p);
    ImGui::Text("%s", theme.name.c_str());
    mutedWrapped(p, "Changes apply instantly. Save them to theme.json to keep them.");
    ImGui::Spacing();

    Theme edited = theme;
    bool changed = false;
    changed |= colorRow("Accent (purple)", edited.palette.accent);
    changed |= colorRow("Accent text", edited.palette.accentText);
    changed |= colorRow("Background", edited.palette.background);
    changed |= colorRow("Panels", edited.palette.surface);
    changed |= colorRow("Text", edited.palette.text);

    if (ImGui::CollapsingHeader("All colours")) {
        changed |= colorRow("Raised panels / inputs", edited.palette.surfaceRaised);
        changed |= colorRow("Borders", edited.palette.border);
        changed |= colorRow("Muted text", edited.palette.textMuted);
        changed |= colorRow("Text on accent", edited.palette.textOnAccent);
        changed |= colorRow("Accent hover", edited.palette.accentHover);
        changed |= colorRow("Accent pressed", edited.palette.accentActive);
        changed |= colorRow("Success", edited.palette.success);
        changed |= colorRow("Warning", edited.palette.warning);
        changed |= colorRow("Error", edited.palette.danger);
    }
    if (ImGui::CollapsingHeader("Shape and size")) {
        changed |= ImGui::SliderFloat("Window corners", &edited.metrics.windowRounding, 0.0f, 24.0f, "%.0f");
        changed |= ImGui::SliderFloat("Control corners", &edited.metrics.frameRounding, 0.0f, 16.0f, "%.0f");
        changed |= ImGui::SliderFloat("Font size", &edited.metrics.fontSize, 12.0f, 24.0f, "%.0f");
    }
    if (changed) {
        edited.name = "Custom";
        ThemeManager::getInstance().setTheme(edited);
    }

    ImGui::Spacing();
    if (widgets::primaryButton("Save theme.json", ImVec2(0, 0), p)) {
        m_themeMessage = ThemeManager::getInstance().saveToFile(m_themeFile) ? "Saved to " + m_themeFile.string()
                                                                              : "Could not write " + m_themeFile.string();
    }
    ImGui::SameLine();
    if (widgets::secondaryButton("Reload", ImVec2(0, 0), p)) {
        std::vector<ThemeIssue> issues;
        if (ThemeManager::getInstance().loadFromFile(m_themeFile, &issues)) {
            m_themeMessage = issues.empty() ? "Reloaded " + m_themeFile.filename().string()
                                            : "Reloaded with " + std::to_string(issues.size()) + " warning(s); see the log.";
        } else {
            m_themeMessage = issues.empty() ? "Could not read " + m_themeFile.string() : issues.front().message;
        }
    }
    ImGui::SameLine();
    if (widgets::secondaryButton("Reset", ImVec2(0, 0), p)) {
        ThemeManager::getInstance().resetToDefault();
        m_themeMessage = "Back to Black & Purple (theme.json is unchanged until you save).";
    }
    if (!m_themeMessage.empty()) mutedWrapped(p, m_themeMessage.c_str());
}

void SettingsPanel::drawSystemSection(const ThemePalette& palette) {
    widgets::sectionLabel("SYSTEM", palette);

    const double now = ImGui::GetTime();
    if (now - m_lastAutostartCheck > 1.0) { // a stat() per frame would be wasteful; once a second is plenty
        m_autostart = m_system.isAutostartEnabled();
        m_lastAutostartCheck = now;
    }
    bool autostart = m_autostart;
    if (ImGui::Checkbox("Run at startup", &autostart)) {
        m_system.setAutostartEnabled(autostart);
        m_autostart = m_system.isAutostartEnabled();
        m_lastAutostartCheck = now;
    }
    ImGui::Spacing();

    if (widgets::secondaryButton("Profiles folder", ImVec2(0, 0), palette)) m_system.openProfilesFolder();
    ImGui::SameLine();
    if (widgets::secondaryButton("config.ini", ImVec2(0, 0), palette)) m_system.openConfigFile();
    ImGui::SameLine();
    if (widgets::secondaryButton("daemon.log", ImVec2(0, 0), palette)) m_system.openLogFile();
}

void SettingsPanel::drawDeviceSection(const AppSnapshot& state, const ThemePalette& palette) {
    widgets::sectionLabel("KEYBOARD", palette);

    const Color dot = state.device.connected ? palette.success : palette.textMuted;
    const std::string status = state.device.connected ? std::string("Connected - ") + transportName(state.device.transport)
                                                       : std::string("Not connected");
    widgets::pill(status.c_str(), palette.surfaceRaised, palette.text, dot);

    if (state.device.connected && !state.device.path.empty()) mutedWrapped(palette, state.device.path.c_str());
    if (state.activeSlot >= 0) {
        const std::string active = "Active slot: " + slotLabel(static_cast<std::size_t>(state.activeSlot));
        mutedWrapped(palette, active.c_str());
    }
}

void SettingsPanel::drawAboutSection(const ThemePalette& palette) {
    widgets::sectionLabel("ABOUT", palette);
    ImGui::Text("KBD Profile Loader %s", KBD_VERSION);
    mutedWrapped(palette, "Switches Keychron K2 HE profiles and tunes actuation from the system tray. The keyboard runs "
                          "Keychron's open-source QMK firmware (github.com/Keychron/qmk_firmware).");
}

} // namespace ui
