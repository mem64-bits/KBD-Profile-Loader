#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "ui/imgui/SettingsPanel.h"
#include "kbd/GameController.h"
#include "ui/ControllerMapping.h"
#include "ui/imgui/Glyphs.h"
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

SettingsPanel::SettingsPanel(ISystemIntegration& system, std::filesystem::path themeFile, std::shared_ptr<Workspace> workspace,
                             std::function<void()> onDisplayChanged)
    : m_system(system), m_themeFile(std::move(themeFile)), m_workspace(workspace ? std::move(workspace) : std::make_shared<Workspace>()),
      m_onDisplayChanged(std::move(onDisplayChanged))
{}

SettingsPanel::~SettingsPanel() = default;

const char* SettingsPanel::title() const noexcept {
    return "Settings";
}

const char* SettingsPanel::subtitle() const noexcept {
    return "Controller artwork, theme, startup and files";
}

widgets::Icon SettingsPanel::icon() const noexcept {
    return widgets::Icon::Settings;
}

void SettingsPanel::render(const AppSnapshot& state, const Theme& theme) {
    const ThemePalette& p = theme.palette;

    drawThemeSection(theme);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    drawControllerSection(state, p);
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

void SettingsPanel::drawControllerSection(const AppSnapshot& state, const ThemePalette& palette) {
    widgets::sectionLabel("KEYBOARD AND CONTROLLER", palette);
    const std::string before = m_workspace->saveDisplay();

    {
        const char* labels[] = {"Auto", "ANSI", "ISO"};
        const int selected = !m_workspace->layout ? 0 : *m_workspace->layout == LayoutKind::Ansi ? 1 : 2;
        ImGui::TextUnformatted("Keyboard drawing");
        const int picked = widgets::segmented("##layout", labels, selected, palette);
        if (picked == 0) m_workspace->layout.reset();
        if (picked == 1) m_workspace->layout = LayoutKind::Ansi;
        if (picked == 2) m_workspace->layout = LayoutKind::Iso;
        if (selected == 0) {
            const std::string detected = std::string("Detected: ") +
                (state.device.connected && *variantName(state.device.variant) ? variantName(state.device.variant) : "nothing yet (ISO drawing)");
            mutedWrapped(palette, detected.c_str());
        }
    }
    ImGui::Spacing();
    {
        const char* labels[] = {"Xbox", "PlayStation"};
        const int selected = m_workspace->labelStyle() == gamepad::LabelStyle::PlayStation ? 1 : 0;
        ImGui::TextUnformatted("Controller buttons and drawing");
        const int picked = widgets::segmented("##labels", labels, selected, palette);
        if (picked == 0) m_workspace->labels = gamepad::LabelStyle::Xbox;
        if (picked == 1) m_workspace->labels = gamepad::LabelStyle::PlayStation;
        mutedWrapped(palette, "Names the buttons and picks the controller drawing (Xbox Series or DualSense). The keyboard "
                              "always reports an Xbox 360 controller to Windows; this only changes the app.");
    }
    ImGui::Spacing();
    {
        const char* labels[] = {"Kenney", "Zacksly", "Text"};
        const int selected = static_cast<int>(m_workspace->glyphs);
        ImGui::TextUnformatted("Button prompts");
        const int picked = widgets::segmented("##glyphs", labels, selected, palette);
        if (picked >= 0) m_workspace->glyphs = static_cast<GlyphStyle>(picked);

        // Live preview of the chosen set, so a click visibly changes something here.
        const float size = ImGui::GetFrameHeight() * 1.25f;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        namespace GT = kbd::GamepadTarget;
        const std::uint8_t sample[] = {GT::button(0), GT::button(1), GT::button(2), GT::button(3), GT::button(4), GT::button(5),
                                       GT::LEFT_TRIGGER, GT::RIGHT_TRIGGER, GT::button(10), GT::button(12), GT::LEFT_STICK_UP,
                                       GT::RIGHT_STICK_RIGHT, GT::button(6), GT::button(7)};
        float x = at.x;
        for (const std::uint8_t target : sample) {
            const ImVec2 c(x + size * 0.5f, at.y + size * 0.55f);
            if (m_workspace->glyphs == GlyphStyle::Drawn ||
                !glyphs::drawTarget(draw, target, m_workspace->labelStyle(), c, size, toImU32(palette.text),
                                    m_workspace->glyphs == GlyphStyle::Zacksly ? glyphs::Pack::Zacksly : glyphs::Pack::Kenney)) {
                const std::string name = mapping::targetName(target, m_workspace->labelStyle());
                const char* text = name.c_str();
                const ImVec2 ts = ImGui::CalcTextSize(text);
                const float w = std::max(size, ts.x + size * 0.4f);
                draw->AddRectFilled(ImVec2(c.x - size * 0.45f, c.y - size * 0.35f), ImVec2(c.x - size * 0.45f + w, c.y + size * 0.35f),
                                    toImU32(palette.surfaceRaised), size * 0.2f);
                draw->AddText(ImVec2(c.x - size * 0.45f + (w - ts.x) * 0.5f, c.y - ts.y * 0.5f), toImU32(palette.text), text);
                x += w - size;
            }
            x += size + size * 0.15f;
        }
        ImGui::Dummy(ImVec2(x - at.x, size * 1.1f));
        mutedWrapped(palette, "Shown on mapped keys, in the controller pick list and in the input history.");
    }
    if (m_workspace->saveDisplay() != before && m_onDisplayChanged) m_onDisplayChanged();
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Artwork credits")) {
        mutedWrapped(palette, "Controller drawing: the \"xbox-controller\" preset from input-overlay by Saikel Orado Liu, "
                              "CC0 1.0 (public domain), with the Xbox logo removed.");
        mutedWrapped(palette, "DualSense drawing (shown with PlayStation names): the \"dualsense\" preset from input-overlay "
                              "by Caro, made from Xelu's controller prompts, CC0 1.0 (public domain), with the "
                              "PlayStation logos removed.");
        mutedWrapped(palette, "Button prompts: \"Input Prompts\" by Kenney (www.kenney.nl), CC0 1.0 (public domain).");
        mutedWrapped(palette, "Xbox Series Button Icons and Controls - Zacksly. Licensed under CC BY 3.0 "
                              "(creativecommons.org/licenses/by/3.0) - https://zacksly.itch.io");
        mutedWrapped(palette, "PS5 Button Icons and Controls - Zacksly. Licensed under CC BY 3.0 "
                              "(creativecommons.org/licenses/by/3.0) - https://zacksly.itch.io");
        mutedWrapped(palette, "Both Zacksly sets are modified: the \"Buttons Full Solid\" icons were resized and packed "
                              "into one image, and the app tints them.");
        mutedWrapped(palette, "Xbox and PlayStation are trademarks of their owners; the names only say which buttons "
                              "are meant.");
    }
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
