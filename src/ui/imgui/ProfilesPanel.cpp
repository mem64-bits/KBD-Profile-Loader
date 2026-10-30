#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "ui/imgui/ProfilesPanel.h"
#include "ui/imgui/ImGuiTheme.h"
#include "ui/imgui/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace ui {

namespace {

    constexpr const char* FLASH_POPUP_TITLE = "Write profile to keyboard";

    std::string upperSlotLabel(const std::size_t slot) {
        std::string label = slotLabel(slot);
        std::transform(label.begin(), label.end(), label.begin(), [](const unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return label;
    }

    void mutedText(const ThemePalette& palette, const char* text) {
        ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(palette.textMuted));
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    }

} // namespace

ProfilesPanel::ProfilesPanel(IProfileCommands& commands, ISystemIntegration& system)
    : m_commands(commands), m_system(system)
{}

ProfilesPanel::~ProfilesPanel() = default;

const char* ProfilesPanel::title() const noexcept {
    return "Profiles";
}

void ProfilesPanel::render(const AppSnapshot& state, const Theme& theme) {
    const ThemePalette& p = theme.palette;

    if (!state.device.connected) {
        ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.warning));
        ImGui::TextUnformatted("Keyboard not connected");
        ImGui::PopStyleColor();
        mutedText(p, "Plug in the K2 HE with USB-C or its 2.4G dongle. The daemon reconnects automatically.");
        ImGui::Spacing();
    }

    for (std::size_t slot = 0; slot < SLOT_COUNT; ++slot) {
        drawCard(state, slot, theme);
        ImGui::Spacing();
    }

    widgets::sectionLabel("PROFILE FILES", p);
    mutedText(p, "Every .json file in your profiles folder is a profile; new files are picked up automatically.");
    if (widgets::secondaryButton("Open profiles folder", ImVec2(0, 0), p)) m_system.openProfilesFolder();

    if (m_openFlashPopup) {
        ImGui::OpenPopup(FLASH_POPUP_TITLE);
        m_openFlashPopup = false;
    }
    drawFlashConfirmation(state, theme);
}

void ProfilesPanel::drawCard(const AppSnapshot& state, const std::size_t slot, const Theme& theme) {
    const ThemePalette& p = theme.palette;
    const SlotInfo& info = state.slots[slot];
    const bool active = state.activeSlot == static_cast<int>(slot);
    const bool canAct = state.device.connected && !state.busy;

    ImGui::PushID(static_cast<int>(slot));
    ImGui::PushStyleColor(ImGuiCol_Border, toImVec4(active ? p.accent : p.border));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toImVec4(active ? p.surface.mix(p.accent, 0.14f) : p.surface));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, active ? 1.6f : 1.0f);
    ImGui::BeginChild("card", ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Row 1: slot / active pills on the left, the actions on the right.
    const float rowTop = ImGui::GetCursorPosY();
    const float rowHeight = ImGui::GetFrameHeight();
    const float pillOffset = (rowHeight - widgets::pillHeight()) * 0.5f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;

    ImGui::SetCursorPosY(rowTop + pillOffset);
    widgets::pill(upperSlotLabel(slot).c_str(), p.surfaceRaised, p.accentText);
    if (active) {
        ImGui::SameLine();
        widgets::pill("ACTIVE", p.accent, p.textOnAccent);
    }

    const char* writeLabel = "Write...";
    const char* activateLabel = "Activate";
    const float actionsWidth = widgets::buttonWidth(writeLabel) + (active ? 0.0f : widgets::buttonWidth(activateLabel) + spacing);
    ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - actionsWidth);
    ImGui::SetCursorPosY(rowTop);

    if (!active) {
        ImGui::BeginDisabled(!canAct);
        if (widgets::primaryButton(activateLabel, ImVec2(0, 0), p)) m_commands.activateSlot(static_cast<std::uint8_t>(slot));
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    ImGui::BeginDisabled(!canAct || !info.hasProfile);
    if (widgets::secondaryButton(writeLabel, ImVec2(0, 0), p)) {
        m_flashSlot = slot;
        m_openFlashPopup = true;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", info.hasProfile ? "Write this profile to the keyboard's flash..." : "This slot has no profile file to write.");
    }

    // Row 2: the profile's name, with the file it comes from muted on the right when there is room for it.
    const char* name = info.hasProfile ? info.name.c_str() : "No profile assigned";
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.2f);
    ImGui::TextUnformatted(name);
    const float nameWidth = ImGui::CalcTextSize(name).x;
    ImGui::PopFont();
    if (info.hasProfile) {
        const float fileWidth = ImGui::CalcTextSize(info.fileName.c_str()).x;
        const float room = ImGui::GetContentRegionAvail().x - nameWidth - spacing * 2.0f;
        if (fileWidth <= room) {
            ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - fileWidth);
            ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.textMuted));
            ImGui::TextUnformatted(info.fileName.c_str());
            ImGui::PopStyleColor();
        } else if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", info.fileName.c_str());
        }
    }

    // Row 3: what the profile contains (wraps instead of clipping).
    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.textMuted));
    if (info.hasProfile) {
        ImGui::TextWrapped("%s", info.storedSettings ? actuation::describe(*info.storedSettings).c_str() : "No actuation data in this file");
    } else {
        ImGui::TextWrapped("Put a .json profile into the profiles folder and set its \"current\" field to this slot.");
    }
    ImGui::PopStyleColor();

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::PopID();
}

void ProfilesPanel::drawFlashConfirmation(const AppSnapshot& state, const Theme& theme) {
    const ThemePalette& p = theme.palette;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.88f, 0.0f));
    if (!ImGui::BeginPopupModal(FLASH_POPUP_TITLE, nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize)) return;

    const SlotInfo& info = state.slots[std::min(m_flashSlot, SLOT_COUNT - 1)];
    ImGui::PushTextWrapPos(0.0f);
    ImGui::Text("Write '%s' to %s?", info.name.c_str(), slotLabel(m_flashSlot).c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    mutedText(p, "The profile's 860-byte memory dump is streamed to the keyboard and committed to its flash, "
                 "replacing what this slot holds now. The result is read back and checked afterwards.");
    ImGui::Spacing();

    if (widgets::primaryButton("Write profile", ImVec2(0, 0), p)) {
        m_commands.flashSlot(static_cast<std::uint8_t>(m_flashSlot));
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (widgets::secondaryButton("Cancel", ImVec2(0, 0), p) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace ui
