#pragma once

#include "ui/IProfileCommands.h"
#include "ui/ISystemIntegration.h"
#include "ui/imgui/IPanel.h"

#include <cstddef>
#include <string>

namespace ui {

    // "Profiles" tab: the keyboard's three slots (activate, write the assigned profile, unassign) and the
    // library of every profile file. Library cards can be dragged by their handle to change the cycling order,
    // or dropped onto a slot to assign them; writing to the keyboard always asks for confirmation first.
    class ProfilesPanel final : public IPanel {
    public:
        ProfilesPanel(IProfileCommands& commands, ISystemIntegration& system);
        ~ProfilesPanel() override;

        ProfilesPanel(const ProfilesPanel&) = delete;
        ProfilesPanel& operator=(const ProfilesPanel&) = delete;
        ProfilesPanel(ProfilesPanel&&) = delete;
        ProfilesPanel& operator=(ProfilesPanel&&) = delete;

        [[nodiscard]] const char* title() const noexcept override;
        [[nodiscard]] const char* subtitle() const noexcept override;
        [[nodiscard]] widgets::Icon icon() const noexcept override;
        void render(const AppSnapshot& state, const Theme& theme) override;

    private:
        void drawSlotCard(const AppSnapshot& state, std::size_t slot, const Theme& theme);
        void drawLibrary(const AppSnapshot& state, const Theme& theme);
        void drawLibraryCard(const AppSnapshot& state, const LibraryProfileInfo& profile, std::size_t index, const Theme& theme);
        void drawWriteConfirmation(const AppSnapshot& state, const Theme& theme);

        // Opens the confirmation for writing `slot`'s profile, or for assigning `fileName` to it and writing that.
        void requestWrite(std::size_t slot, std::string fileName = {});

        IProfileCommands&   m_commands;
        ISystemIntegration& m_system;
        std::size_t         m_writeSlot{0};
        std::string         m_writeFileName; // empty: write the profile already assigned to m_writeSlot
        bool                m_openWritePopup{false};
    };

} // namespace ui
