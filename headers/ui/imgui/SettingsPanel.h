#pragma once

#include "ui/ISystemIntegration.h"
#include "ui/imgui/IPanel.h"

#include <filesystem>
#include <string>

namespace ui {

    // "Settings" tab: live theme editing (saved to theme.json), run-at-startup, shortcuts to the daemon's
    // files, device details and an About section.
    class SettingsPanel final : public IPanel {
    public:
        SettingsPanel(ISystemIntegration& system, std::filesystem::path themeFile);
        ~SettingsPanel() override;

        SettingsPanel(const SettingsPanel&) = delete;
        SettingsPanel& operator=(const SettingsPanel&) = delete;
        SettingsPanel(SettingsPanel&&) = delete;
        SettingsPanel& operator=(SettingsPanel&&) = delete;

        [[nodiscard]] const char* title() const noexcept override;
        void render(const AppSnapshot& state, const Theme& theme) override;

    private:
        void drawThemeSection(const Theme& theme);
        void drawSystemSection(const ThemePalette& palette);
        void drawDeviceSection(const AppSnapshot& state, const ThemePalette& palette);
        void drawAboutSection(const ThemePalette& palette);

        ISystemIntegration&   m_system;
        std::filesystem::path m_themeFile;
        std::string           m_themeMessage;
        bool                  m_autostart{false};
        double                m_lastAutostartCheck{-10.0};
    };

} // namespace ui
