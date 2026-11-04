#pragma once

#include "ui/AppModel.h"
#include "ui/IView.h"
#include "ui/IProfileCommands.h"
#include "ui/imgui/IPanel.h"
#include "ui/imgui/Workspace.h"

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace ui {

    // The application window's content: a sidebar (app title, one entry per page, the keyboard's connection and
    // active profile) and the selected page with its title and a status line. It fills the whole window, so the
    // window (not an ImGui sub-window) is what the user moves and resizes. Ctrl+1 .. Ctrl+9 switch pages.
    class DashboardView final : public IView {
    public:
        DashboardView(AppModel& model, IProfileCommands& commands, std::shared_ptr<Workspace> workspace);
        ~DashboardView() override;

        DashboardView(const DashboardView&) = delete;
        DashboardView& operator=(const DashboardView&) = delete;
        DashboardView(DashboardView&&) = delete;
        DashboardView& operator=(DashboardView&&) = delete;

        void addPanel(std::unique_ptr<IPanel> panel);

        // Jumps to a page (by index) on the next frame; used for screenshots and by the --tab option.
        void selectTab(std::size_t index);

        void render() override;
        [[nodiscard]] bool isVisible() const noexcept override;
        void setVisible(bool visible) noexcept override;

    private:
        void drawSidebar(const AppSnapshot& state, const Theme& theme, float width);
        void drawPageHeader(const IPanel& panel, const AppSnapshot& state, const Theme& theme);
        void drawSlotSwitcher(const AppSnapshot& state, const ThemePalette& p, float width);
        void readSlotIfNeeded(const AppSnapshot& state);
        float footerHeight(const AppSnapshot& state, float wrapWidth) const;
        void drawFooter(const AppSnapshot& state, const Theme& theme);
        void switchTo(std::size_t index);

        AppModel&                           m_model;
        IProfileCommands&                   m_commands;
        std::shared_ptr<Workspace>          m_workspace;
        std::vector<std::unique_ptr<IPanel>> m_panels;
        std::atomic<bool>                   m_visible{false};
        std::optional<std::size_t>          m_pendingTab;
        std::size_t                         m_current{0};
        bool                                m_currentShown{false}; // onShown() was called for m_current
    };

} // namespace ui
