#include "ui/imgui/KeyboardPage.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <span>

namespace ui {

void KeyboardPage::addTab(std::unique_ptr<IPanel> tab) {
    if (tab) m_tabs.push_back(std::move(tab));
}

void KeyboardPage::selectTab(const std::size_t index) {
    if (index >= m_tabs.size() || index == m_current) return;
    if (m_shown && m_current < m_tabs.size()) m_tabs[m_current]->onHidden();
    m_current = index;
    if (m_shown) m_tabs[m_current]->onShown();
}

const char* KeyboardPage::subtitle() const noexcept {
    return m_current < m_tabs.size() ? m_tabs[m_current]->subtitle() : "";
}

void KeyboardPage::onShown() {
    m_shown = true;
    if (m_current < m_tabs.size()) m_tabs[m_current]->onShown();
}

void KeyboardPage::onHidden() {
    if (m_shown && m_current < m_tabs.size()) m_tabs[m_current]->onHidden();
    m_shown = false;
}

void KeyboardPage::render(const AppSnapshot& state, const Theme& theme) {
    if (m_tabs.empty()) return;

    std::array<const char*, 8> labels{};
    const std::size_t count = std::min(m_tabs.size(), labels.size());
    for (std::size_t i = 0; i < count; ++i) labels[i] = m_tabs[i]->title();

    const int picked = widgets::segmented("##keyboard_tabs", std::span<const char* const>(labels.data(), count),
                                          static_cast<int>(m_current), theme.palette);
    if (picked >= 0) selectTab(static_cast<std::size_t>(picked));
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));

    m_tabs[m_current]->render(state, theme);
}

} // namespace ui
