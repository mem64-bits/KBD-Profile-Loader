#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "ui/imgui/DashboardView.h"
#include "ui/ThemeManager.h"
#include "ui/imgui/ImGuiTheme.h"
#include "ui/imgui/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace ui {

DashboardView::DashboardView(AppModel& model) : m_model(model) {}

DashboardView::~DashboardView() = default;

void DashboardView::addPanel(std::unique_ptr<IPanel> panel) {
    if (panel) m_panels.push_back(std::move(panel));
}

void DashboardView::selectTab(const std::size_t index) {
    m_pendingTab = index;
}

bool DashboardView::isVisible() const noexcept {
    return m_visible.load(std::memory_order_acquire);
}

void DashboardView::setVisible(const bool visible) noexcept {
    m_visible.store(visible, std::memory_order_release);
}

void DashboardView::drawHeader(const AppSnapshot& state, const Theme& theme) {
    const ThemePalette& p = theme.palette;

    // Title on the left ...
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.45f);
    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.accentText));
    ImGui::TextUnformatted("KEYCHRON K2 HE");
    ImGui::PopStyleColor();
    ImGui::PopFont();

    // ... connection badge on the right, level with it.
    const std::string badge = state.device.connected ? transportName(state.device.transport) : "Disconnected";
    const ImGuiStyle& style = ImGui::GetStyle();
    const float badgeWidth = ImGui::CalcTextSize(badge.c_str()).x + style.FramePadding.x * 1.6f + ImGui::GetTextLineHeight() * 0.45f + style.FramePadding.x * 0.6f;
    ImGui::SameLine(ImGui::GetWindowWidth() - badgeWidth - style.WindowPadding.x);
    widgets::pill(badge.c_str(), p.surfaceRaised, p.text, state.device.connected ? p.success : p.textMuted);

    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.textMuted));
    ImGui::TextUnformatted("Profile switcher and actuation");
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

float DashboardView::footerHeight(const AppSnapshot& state, const float wrapWidth) const {
    const ImGuiStyle& style = ImGui::GetStyle();
    const std::string text = state.status.text.empty() ? "Ready." : state.status.text;
    const float indent = ImGui::GetTextLineHeight() * 1.2f;
    const ImVec2 size = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrapWidth - indent);
    return size.y + style.ItemSpacing.y * 2.0f + 2.0f;
}

void DashboardView::drawFooter(const AppSnapshot& state, const Theme& theme) {
    const ThemePalette& p = theme.palette;
    const bool hasStatus = !state.status.text.empty();
    const Color color = state.busy ? p.accentText : statusColor(p, hasStatus ? state.status.severity : StatusSeverity::None);
    const float indent = ImGui::GetTextLineHeight() * 1.2f;

    ImGui::Separator();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    if (state.busy) {
        widgets::spinner(ImGui::GetTextLineHeight() * 0.38f, color);
    } else {
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(origin.x + ImGui::GetTextLineHeight() * 0.35f, origin.y + ImGui::GetTextLineHeight() * 0.5f + 2.0f),
                                                    ImGui::GetTextLineHeight() * 0.2f, toImU32(color));
    }

    ImGui::SetCursorScreenPos(ImVec2(origin.x + indent, origin.y));
    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(hasStatus ? p.text : p.textMuted));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextUnformatted(hasStatus ? state.status.text.c_str() : "Ready.");
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void DashboardView::render() {
    if (!isVisible()) return;

    const AppSnapshot state = m_model.snapshot();
    const Theme theme = ThemeManager::getInstance().getTheme();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    // The OS window already has its own frame: draw an undecorated, borderless ImGui window that fills it.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    constexpr ImGuiWindowFlags FLAGS = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    const bool open = ImGui::Begin("##dashboard", nullptr, FLAGS);
    ImGui::PopStyleVar(2);

    if (open) {
        drawHeader(state, theme);

        const float footer = footerHeight(state, ImGui::GetContentRegionAvail().x);
        if (ImGui::BeginTabBar("##tabs")) {
            for (std::size_t i = 0; i < m_panels.size(); ++i) {
                ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
                if (m_pendingTab && *m_pendingTab == i) {
                    flags |= ImGuiTabItemFlags_SetSelected;
                    m_pendingTab.reset();
                }
                if (!ImGui::BeginTabItem(m_panels[i]->title(), nullptr, flags)) continue;

                // The panel scrolls on its own; the status line below it never moves.
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
                ImGui::BeginChild("##content", ImVec2(0, -footer), ImGuiChildFlags_None);
                ImGui::Dummy(ImVec2(0, ImGui::GetStyle().ItemSpacing.y * 0.5f));
                m_panels[i]->render(state, theme);
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        drawFooter(state, theme);
    }
    ImGui::End();
}

} // namespace ui
