#pragma once

#include "ui/IProfileCommands.h"
#include "ui/ISystemIntegration.h"
#include "ui/imgui/IPanel.h"

#include <cstddef>

namespace ui {

    // "Profiles" tab: one card per keyboard slot with its profile, an Activate button and a
    // (confirmed) "write profile to keyboard" action.
    class ProfilesPanel final : public IPanel {
    public:
        ProfilesPanel(IProfileCommands& commands, ISystemIntegration& system);
        ~ProfilesPanel() override;

        ProfilesPanel(const ProfilesPanel&) = delete;
        ProfilesPanel& operator=(const ProfilesPanel&) = delete;
        ProfilesPanel(ProfilesPanel&&) = delete;
        ProfilesPanel& operator=(ProfilesPanel&&) = delete;

        [[nodiscard]] const char* title() const noexcept override;
        void render(const AppSnapshot& state, const Theme& theme) override;

    private:
        void drawCard(const AppSnapshot& state, std::size_t slot, const Theme& theme);
        void drawFlashConfirmation(const AppSnapshot& state, const Theme& theme);

        IProfileCommands&   m_commands;
        ISystemIntegration& m_system;
        std::size_t         m_flashSlot{0};
        bool                m_openFlashPopup{false};
    };

} // namespace ui
