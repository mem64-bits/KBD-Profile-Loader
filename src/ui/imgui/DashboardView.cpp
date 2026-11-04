#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "ui/imgui/DashboardView.h"
#include "ui/ThemeManager.h"
#include "ui/imgui/ImGuiTheme.h"
#include "ui/imgui/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace ui {

namespace {

    constexpr float SIDEBAR_EM = 13.5f; // sidebar width in font sizes

    // The app's mark: a keycap seen from above, in the accent colour.
    void drawLogo(ImDrawList* draw, const ImVec2 min, const float size, const ThemePalette& p) {
        const ImVec2 max(min.x + size, min.y + size);
        draw->AddRectFilled(min, max, toImU32(p.accent), size * 0.24f);
        const float inset = size * 0.16f;
        draw->AddRectFilled(ImVec2(min.x + inset, min.y + inset * 0.7f), ImVec2(max.x - inset, max.y - inset * 1.5f),
                            toImU32(p.accentHover.lighten(0.12f)), size * 0.16f);
        ImFont* font = ImGui::GetFont();
        const float textPx = size * 0.38f;
        const char* text = "HE";
        const ImVec2 textSize = font->CalcTextSizeA(textPx, FLT_MAX, 0.0f, text);
        draw->AddText(font, textPx, ImVec2(min.x + (size - textSize.x) * 0.5f, min.y + (size - inset * 0.8f - textSize.y) * 0.5f),
                      toImU32(p.textOnAccent), text);
    }

} // namespace

DashboardView::DashboardView(AppModel& model, IProfileCommands& commands, std::shared_ptr<Workspace> workspace)
    : m_model(model), m_commands(commands), m_workspace(workspace ? std::move(workspace) : std::make_shared<Workspace>()) {}

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
    // Called on the UI thread when the window is shown / hidden: a hidden page stops its live polling.
    if (!visible && m_currentShown && m_current < m_panels.size()) {
        try {
            m_panels[m_current]->onHidden();
        } catch (...) {
        }
        m_currentShown = false;
    }
}

void DashboardView::switchTo(const std::size_t index) {
    if (index >= m_panels.size()) return;
    if (index == m_current && m_currentShown) return;
    if (m_currentShown && m_current < m_panels.size()) m_panels[m_current]->onHidden();
    m_current      = index;
    m_currentShown = false; // onShown() runs right before the page renders
}

void DashboardView::drawSidebar(const AppSnapshot& state, const Theme& theme, const float width) {
    const ThemePalette& p = theme.palette;
    const ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float em = ImGui::GetFontSize();

    // Brand.
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float logo = em * 2.3f;
    drawLogo(draw, origin, logo, p);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + logo + em * 0.7f, origin.y + logo * 0.5f - em * 1.05f));
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, style.FontSizeBase * 1.15f);
    ImGui::TextUnformatted("K2 HE");
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.textMuted));
    ImGui::TextUnformatted("Profile Loader");
    ImGui::PopStyleColor();
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + logo + em * 1.3f));

    // Navigation.
    const float itemHeight = em * 2.35f;
    const float itemWidth  = width - style.WindowPadding.x * 2.0f;
    for (std::size_t i = 0; i < m_panels.size(); ++i) {
        const IPanel& panel = *m_panels[i];
        ImGui::PushID(static_cast<int>(i));
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(min.x + itemWidth, min.y + itemHeight);
        if (ImGui::InvisibleButton("##nav", ImVec2(itemWidth, itemHeight))) switchTo(i);
        const bool hovered  = ImGui::IsItemHovered();
        const bool selected = i == m_current;
        if (hovered && i < 9) ImGui::SetTooltip("Ctrl+%d", static_cast<int>(i + 1));

        if (selected) {
            draw->AddRectFilled(min, max, toImU32(p.accent.withAlpha(46)), em * 0.45f);
            draw->AddRectFilled(ImVec2(min.x, min.y + itemHeight * 0.22f), ImVec2(min.x + em * 0.2f, max.y - itemHeight * 0.22f),
                                toImU32(p.accent), em * 0.1f);
        } else if (hovered) {
            draw->AddRectFilled(min, max, toImU32(p.surfaceRaised), em * 0.45f);
        }
        const Color ink = selected ? p.accentText : hovered ? p.text : p.textMuted;
        widgets::drawIcon(draw, panel.icon(), ImVec2(min.x + em * 1.5f, min.y + itemHeight * 0.5f), em * 1.05f, toImU32(ink),
                          std::max(1.3f, em * 0.09f));
        draw->AddText(ImVec2(min.x + em * 2.9f, min.y + (itemHeight - ImGui::GetTextLineHeight()) * 0.5f),
                      toImU32(selected ? p.text : ink), panel.title());
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, em * 0.15f));
    }

    // Keyboard status, pinned to the bottom of the sidebar.
    const float cardHeight = em * 5.6f;
    const float bottom = ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - style.WindowPadding.y;
    const float cardTop = std::max(ImGui::GetCursorScreenPos().y + em, bottom - cardHeight);
    const ImVec2 cardMin(origin.x, cardTop);
    const ImVec2 cardMax(origin.x + itemWidth, cardTop + cardHeight);
    draw->AddRectFilled(cardMin, cardMax, toImU32(p.surfaceRaised.mix(p.background, 0.25f)), em * 0.5f);
    draw->AddRect(cardMin, cardMax, toImU32(p.border), em * 0.5f);

    const float pad = em * 0.75f;
    float y = cardMin.y + pad;
    const Color dot = state.device.connected ? p.success : p.danger;
    draw->AddCircleFilled(ImVec2(cardMin.x + pad + em * 0.25f, y + em * 0.55f), em * 0.25f, toImU32(dot));
    draw->AddText(ImVec2(cardMin.x + pad + em * 0.8f, y), toImU32(p.text), state.device.connected ? "Connected" : "Not connected");
    y += em * 1.35f;
    std::string line = state.device.connected ? std::string(transportName(state.device.transport)) : "Plug in the USB-C cable";
    if (state.device.connected && variantName(state.device.variant)[0] != '\0') line += std::string("  -  ") + variantName(state.device.variant);
    draw->AddText(ImVec2(cardMin.x + pad, y), toImU32(p.textMuted), line.c_str());
    y += em * 1.3f;
    if (state.device.connected && state.activeSlot >= 0 && state.activeSlot < static_cast<int>(SLOT_COUNT)) {
        const SlotInfo& slot = state.slots[static_cast<std::size_t>(state.activeSlot)];
        std::string active = slotLabel(static_cast<std::size_t>(state.activeSlot)) + ": " + (slot.hasProfile ? slot.name : "no profile file");
        draw->PushClipRect(cardMin, ImVec2(cardMax.x - pad * 0.5f, cardMax.y), true);
        draw->AddText(ImVec2(cardMin.x + pad, y), toImU32(p.accentText), active.c_str());
        draw->PopClipRect();
    }
}

void DashboardView::readSlotIfNeeded(const AppSnapshot& state) {
    if (!state.device.connected || state.busy) return;
    const std::size_t slot = m_workspace->slot(state);
    if (!state.actuation[slot] && !state.keyMatrix[slot] && !m_workspace->actuationRequested[slot]) {
        m_workspace->actuationRequested[slot] = true;
        m_commands.readActuation(static_cast<std::uint8_t>(slot));
    } else if (!state.details[slot] && !m_workspace->detailsRequested[slot]) {
        m_workspace->detailsRequested[slot] = true;
        m_commands.readSlotDetails(static_cast<std::uint8_t>(slot));
    }
}

void DashboardView::drawSlotSwitcher(const AppSnapshot& state, const ThemePalette& p, const float width) {
    const float em = ImGui::GetFontSize();
    const float spacing = em * 0.4f;
    const float chipH = em * 2.6f;
    const float chipW = std::floor((width - spacing * 2.0f) / 3.0f);
    const std::size_t selected = m_workspace->slot(state);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    for (std::size_t i = 0; i < SLOT_COUNT; ++i) {
        if (i > 0) ImGui::SameLine(0.0f, spacing);
        ImGui::PushID(static_cast<int>(i));
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(min.x + chipW, min.y + chipH);
        if (ImGui::InvisibleButton("##slot", ImVec2(chipW, chipH))) {
            // Clicking the active slot goes back to "follow the keyboard".
            m_workspace->pickedSlot = static_cast<int>(i) == state.activeSlot ? std::nullopt : std::optional<std::size_t>(i);
        }
        const bool hovered = ImGui::IsItemHovered();
        const bool isSelected = i == selected;
        const bool isActive = static_cast<int>(i) == state.activeSlot;
        const std::size_t pads = state.details[i] ? gamepadKeyCount(*state.details[i]) : 0;

        const Color fill = isSelected ? p.accent.withAlpha(52) : hovered ? p.surfaceRaised : p.surface;
        draw->AddRectFilled(min, max, toImU32(fill), em * 0.55f);
        draw->AddRect(min, max, toImU32(isSelected ? p.accent : p.border), em * 0.55f, 0, isSelected ? 1.6f : 1.0f);

        // Number badge.
        const float badge = em * 1.45f;
        const ImVec2 badgeMin(min.x + em * 0.55f, min.y + (chipH - badge) * 0.5f);
        draw->AddRectFilled(badgeMin, badgeMin + ImVec2(badge, badge), toImU32(isSelected ? p.accent : p.surfaceRaised.lighten(0.05f)), em * 0.4f);
        const std::string number = std::to_string(i + 1);
        const ImVec2 numberSize = ImGui::CalcTextSize(number.c_str());
        draw->AddText(badgeMin + (ImVec2(badge, badge) - numberSize) * 0.5f, toImU32(isSelected ? p.textOnAccent : p.text), number.c_str());

        // Name and a status line.
        const float textX = badgeMin.x + badge + em * 0.55f;
        const float textRight = max.x - em * (pads > 0 ? 2.1f : 0.6f);
        const std::string name = state.slots[i].hasProfile ? state.slots[i].name : slotLabel(i);
        draw->PushClipRect(ImVec2(textX, min.y), ImVec2(textRight, max.y), true);
        draw->AddText(ImVec2(textX, min.y + chipH * 0.5f - em * 1.05f), toImU32(p.text), name.c_str());
        const char* status = isActive ? "On the keyboard" : "Stored";
        draw->AddText(ImVec2(textX + (isActive ? em * 0.7f : 0.0f), min.y + chipH * 0.5f + em * 0.05f),
                      toImU32(isActive ? p.success : p.textMuted), status);
        if (isActive) draw->AddCircleFilled(ImVec2(textX + em * 0.25f, min.y + chipH * 0.5f + em * 0.6f), em * 0.22f, toImU32(p.success));
        draw->PopClipRect();

        // Gamepad profile mark.
        if (pads > 0) {
            widgets::drawIcon(draw, widgets::Icon::Gamepad, ImVec2(max.x - em * 1.15f, min.y + chipH * 0.5f), em * 1.0f,
                              toImU32(isSelected ? p.accentText : p.textMuted), std::max(1.3f, em * 0.085f));
        }
        if (hovered) {
            std::string tip = isActive ? "Active on the keyboard" : "Edit this slot (it stays inactive until you activate it)";
            if (pads > 0) tip += "\nGamepad profile: " + std::to_string(pads) + " keys drive the controller";
            if (isActive && m_workspace->pickedSlot) tip += "\nClick to follow the keyboard's active slot again";
            ImGui::SetTooltip("%s", tip.c_str());
        }
        ImGui::PopID();
    }
}

void DashboardView::drawPageHeader(const IPanel& panel, const AppSnapshot& state, const Theme& theme) {
    const ThemePalette& p = theme.palette;
    const float em = ImGui::GetFontSize();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float avail = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize;

    // Row 1: the page title, and on slot pages the slot switcher filling the rest of the row (so slot names
    // have room). Row 2: the subtitle across the whole width.
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5f);
    const ImVec2 titleSize = ImGui::CalcTextSize(panel.title());
    ImGui::PopFont();
    float rowBottom = start.y + titleSize.y;
    if (panel.editsSlot()) {
        const float switcherWidth = std::max(em * 18.0f, avail - titleSize.x - em * 2.0f);
        ImGui::SetCursorScreenPos(ImVec2(start.x + avail - switcherWidth, start.y));
        ImGui::BeginGroup();
        drawSlotSwitcher(state, p, switcherWidth);
        ImGui::EndGroup();
        rowBottom = std::max(rowBottom, ImGui::GetItemRectMax().y);
    }
    ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + std::max(0.0f, (rowBottom - start.y - titleSize.y) * 0.5f)));
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5f);
    ImGui::TextUnformatted(panel.title());
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(start.x, rowBottom + ImGui::GetStyle().ItemSpacing.y * 0.6f));

    if (panel.subtitle()[0] != '\0') {
        ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.textMuted));
        ImGui::PushTextWrapPos(start.x - ImGui::GetWindowPos().x + avail);
        ImGui::TextWrapped("%s", panel.subtitle());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

    // The controller mode only changes after a replug: say so on every page until the keyboard reconnects.
    if (state.controller.replugNeeded && state.device.connected) {
        ImGui::Spacing();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetFrameHeight() * 1.25f;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(at, ImVec2(at.x + avail, at.y + h), toImU32(p.warning.withAlpha(38)), ImGui::GetStyle().FrameRounding);
        draw->AddRect(at, ImVec2(at.x + avail, at.y + h), toImU32(p.warning.withAlpha(140)), ImGui::GetStyle().FrameRounding);
        const char* text = "Unplug the keyboard's cable and plug it back in: the new controller mode starts after a replug.";
        draw->AddText(ImVec2(at.x + em * 0.8f, at.y + (h - ImGui::GetTextLineHeight()) * 0.5f), toImU32(p.text), text);
        ImGui::Dummy(ImVec2(avail, h));
    }
    ImGui::Dummy(ImVec2(0, ImGui::GetStyle().ItemSpacing.y * 0.6f));
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
    const ThemePalette& p = theme.palette;

    if (m_pendingTab) {
        switchTo(*m_pendingTab);
        m_pendingTab.reset();
    }
    // Ctrl+1 .. Ctrl+9 jump to a page.
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && !io.WantTextInput) {
        for (std::size_t i = 0; i < std::min<std::size_t>(m_panels.size(), 9); ++i) {
            if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + static_cast<int>(i)), false)) switchTo(i);
        }
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    // The OS window already has its own frame: draw an undecorated, borderless ImGui window that fills it.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    constexpr ImGuiWindowFlags FLAGS = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    const bool open = ImGui::Begin("##dashboard", nullptr, FLAGS);
    ImGui::PopStyleVar(3);

    if (open && !m_panels.empty()) {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float sidebarWidth = std::floor(ImGui::GetFontSize() * SIDEBAR_EM);

        // --- sidebar ---
        ImGui::PushStyleColor(ImGuiCol_ChildBg, toImVec4(p.surface.mix(p.background, 0.35f)));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.FramePadding.x * 1.4f, style.FramePadding.y * 3.0f));
        ImGui::BeginChild("##sidebar", ImVec2(sidebarWidth, 0), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
        drawSidebar(state, theme, sidebarWidth);
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        // Hairline between the sidebar and the page.
        {
            const ImVec2 at = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(at.x, ImGui::GetItemRectMin().y), at, toImU32(p.border));
        }

        // --- page ---
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.FramePadding.x * 2.4f, style.FramePadding.y * 3.0f));
        ImGui::BeginChild("##page", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
        IPanel& panel = *m_panels[m_current];
        if (!m_currentShown) {
            panel.onShown();
            m_currentShown = true;
        }
        if (panel.editsSlot()) readSlotIfNeeded(state);
        drawPageHeader(panel, state, theme);

        const float footer = footerHeight(state, ImGui::GetContentRegionAvail().x);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##content", ImVec2(0, -footer), ImGuiChildFlags_None);
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0, style.ItemSpacing.y * 0.5f));
        // Keep a little room on the right for the scroll bar so content never sits under it.
        ImGui::PushItemWidth(-style.ScrollbarSize);
        panel.render(state, theme);
        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::PopStyleColor();

        drawFooter(state, theme);
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }
    ImGui::End();
}

} // namespace ui
