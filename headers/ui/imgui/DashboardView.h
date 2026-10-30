#pragma once

#include "ui/AppModel.h"
#include "ui/IView.h"
#include "ui/imgui/IPanel.h"

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace ui {

    // The application window's content: title, connection state, one tab per panel and a status line.
    // It fills the whole window, so the window (not an ImGui sub-window) is what the user moves and resizes.
    class DashboardView final : public IView {
    public:
        explicit DashboardView(AppModel& model);
        ~DashboardView() override;

        DashboardView(const DashboardView&) = delete;
        DashboardView& operator=(const DashboardView&) = delete;
        DashboardView(DashboardView&&) = delete;
        DashboardView& operator=(DashboardView&&) = delete;

        void addPanel(std::unique_ptr<IPanel> panel);

        // Jumps to a tab (by index) on the next frame; used for screenshots and by the --tab option.
        void selectTab(std::size_t index);

        void render() override;
        [[nodiscard]] bool isVisible() const noexcept override;
        void setVisible(bool visible) noexcept override;

    private:
        void drawHeader(const AppSnapshot& state, const Theme& theme);
        float footerHeight(const AppSnapshot& state, float wrapWidth) const;
        void drawFooter(const AppSnapshot& state, const Theme& theme);

        AppModel&                           m_model;
        std::vector<std::unique_ptr<IPanel>> m_panels;
        std::atomic<bool>                   m_visible{false};
        std::optional<std::size_t>          m_pendingTab;
    };

} // namespace ui
