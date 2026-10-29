#include "ui/QuickProfilePickerView.h"
#include "files/ProfileService.h"
#include <imgui.h>

namespace UI {

QuickProfilePickerView::QuickProfilePickerView(kbd::ProfileService& profileService)
    : m_profileService(profileService)
{}

void QuickProfilePickerView::render() {
    if (!m_visible) return;

    // Window setup: Fixed size popup near bottom-right taskbar
    ImGui::SetNextWindowSize(ImVec2(360, 420), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoScrollbar;

    if (ImGui::Begin("Keychron K2 HE - Quick Profile Switcher", &m_visible, flags)) {
        int activeSlot = m_profileService.queryActiveHardwareSlot();

        ImGui::TextColored(ImVec4(0.24f, 0.60f, 0.95f, 1.0f), "HARDWARE STATUS");
        ImGui::SameLine();
        ImGui::TextDisabled("| Active Slot: %d", activeSlot + 1);
        ImGui::Separator();

        ImGui::Spacing();
        ImGui::Text("Loaded Profiles (%zu available):", m_profileService.profileCount());
        ImGui::Spacing();

        // Render each profile as an interactive card
        // (Iterates through our dsa::DLL library)
        for (uint8_t slot = 0; slot < 3; ++slot) {
            std::string name = m_profileService.getProfileNameForSlot(slot);
            bool isActive = (slot == activeSlot);

            ImGui::PushID(slot);

            // Active slot gets a highlighted frame
            if (isActive) {
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.24f, 0.60f, 0.95f, 1.0f));
            }

            ImGui::BeginChild(("card_" + std::to_string(slot)).c_str(), ImVec2(0, 75), true);

            // Slot tag & Profile Title
            ImGui::TextColored(isActive ? ImVec4(0.16f, 0.80f, 0.53f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                               "SLOT %d", slot + 1);
            ImGui::SameLine();
            ImGui::TextUnformatted(name.c_str());

            ImGui::Spacing();

            // 1-Click Activate Button
            if (isActive) {
                ImGui::TextColored(ImVec4(0.16f, 0.80f, 0.53f, 1.0f), "[ACTIVE ON HARDWARE]");
            } else {
                if (ImGui::Button("Activate", ImVec2(90, 24))) {
                    m_profileService.switchHardwareSlot(slot);
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Burn to Flash (0x1F)", ImVec2(140, 24))) {
                // Permanently flashes this slot
                if (m_profileService.profileCount() > 0) {
                    m_profileService.flashProfileMemory(slot, m_profileService.getCurrentProfile());
                }
            }

            ImGui::EndChild();

            if (isActive) {
                ImGui::PopStyleColor();
            }

            ImGui::PopID();
            ImGui::Spacing();
        }

        ImGui::Separator();
        if (ImGui::Button("Close Window", ImVec2(-1, 28))) {
            m_visible = false;
        }
    }
    ImGui::End();
}

} // namespace ui