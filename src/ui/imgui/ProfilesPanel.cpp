#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "ui/imgui/ProfilesPanel.h"
#include "ui/imgui/ImGuiTheme.h"
#include "ui/imgui/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace ui {

namespace {

    constexpr const char* WRITE_POPUP_TITLE = "Write profile to keyboard";

    // One drag carries one payload (a second SetDragDropPayload would replace the first), so the library card
    // sends both what a slot needs (the file) and what the library needs (the position).
    constexpr const char* PROFILE_PAYLOAD = "KBD_PROFILE";
    struct ProfilePayload {
        std::size_t index{0};
        char        fileName[260]{};
    };

    std::string upperSlotLabel(const std::size_t slot) {
        std::string label = slotLabel(slot);
        std::transform(label.begin(), label.end(), label.begin(),
                       [](const unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return label;
    }

    void mutedText(const ThemePalette& palette, const char* text) {
        ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(palette.textMuted));
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    }

    std::string plural(const std::size_t n, const char* one, const char* many) {
        return std::to_string(n) + " " + (n == 1 ? one : many);
    }

    std::string describeTravel(const std::optional<ActuationSettings>& global, const std::size_t keyOverrides,
                               const ProfileExtras& extras) {
        if (!global) return "No actuation data in this file";
        std::string text = actuation::describe(*global);
        if (keyOverrides > 0) text += " + " + plural(keyOverrides, "key", "keys") + " with own settings";
        if (extras.socdPairs > 0) text += ", " + plural(extras.socdPairs, "SOCD pair", "SOCD pairs");
        if (extras.gamepadKeys > 0) text += ", " + plural(extras.gamepadKeys, "gamepad key", "gamepad keys");
        if (extras.toggleKeys > 0) text += ", " + plural(extras.toggleKeys, "toggle key", "toggle keys");
        if (extras.dynamicKeys > 0) text += ", " + plural(extras.dynamicKeys, "dynamic-keystroke key", "dynamic-keystroke keys");
        return text;
    }

    // Extras of the file behind a slot (the library has them; SlotInfo only carries the travel settings).
    ProfileExtras extrasFor(const AppSnapshot& state, const std::string& fileName) {
        const auto it = std::find_if(state.library.begin(), state.library.end(),
                                     [&](const LibraryProfileInfo& profile) { return profile.fileName == fileName; });
        return it != state.library.end() ? it->extras : ProfileExtras{};
    }

    const ProfilePayload* acceptProfile() {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(PROFILE_PAYLOAD);
        if (payload == nullptr || payload->DataSize != static_cast<int>(sizeof(ProfilePayload))) return nullptr;
        return static_cast<const ProfilePayload*>(payload->Data);
    }

} // namespace

ProfilesPanel::ProfilesPanel(IProfileCommands& commands, ISystemIntegration& system)
    : m_commands(commands), m_system(system)
{}

ProfilesPanel::~ProfilesPanel() = default;

const char* ProfilesPanel::title() const noexcept {
    return "Library";
}

const char* ProfilesPanel::subtitle() const noexcept {
    return "The keyboard's three slots and your profile library";
}

widgets::Icon ProfilesPanel::icon() const noexcept {
    return widgets::Icon::Profiles;
}

void ProfilesPanel::requestWrite(const std::size_t slot, std::string fileName) {
    m_writeSlot      = slot % SLOT_COUNT;
    m_writeFileName  = std::move(fileName);
    m_openWritePopup = true;
}

void ProfilesPanel::render(const AppSnapshot& state, const Theme& theme) {
    const ThemePalette& p = theme.palette;

    if (!state.device.connected) {
        ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.warning));
        ImGui::TextUnformatted("Keyboard not connected");
        ImGui::PopStyleColor();
        mutedText(p, "Connect the K2 HE with its USB-C cable (settings can only be changed wired). It is picked up automatically.");
        ImGui::Spacing();
    }

    widgets::sectionLabel("KEYBOARD SLOTS", p);
    for (std::size_t slot = 0; slot < SLOT_COUNT; ++slot) {
        drawSlotCard(state, slot, theme);
        ImGui::Spacing();
    }

    ImGui::Spacing();
    drawLibrary(state, theme);

    if (m_openWritePopup) {
        ImGui::OpenPopup(WRITE_POPUP_TITLE);
        m_openWritePopup = false;
    }
    drawWriteConfirmation(state, theme);
}

// -----------------------------------------------------------------------------
// Slots
// -----------------------------------------------------------------------------
void ProfilesPanel::drawSlotCard(const AppSnapshot& state, const std::size_t slot, const Theme& theme) {
    const ThemePalette& p = theme.palette;
    const SlotInfo& info = state.slots[slot];
    const bool active = state.activeSlot == static_cast<int>(slot);
    const bool canAct = state.device.connected && !state.busy;

    ImGui::PushID(static_cast<int>(slot));
    ImGui::PushStyleColor(ImGuiCol_Border, toImVec4(active ? p.accent : p.border));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toImVec4(active ? p.surface.mix(p.accent, 0.14f) : p.surface));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, active ? 1.6f : 1.0f);
    ImGui::BeginChild("slot_card", ImVec2(0, 0),
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

    const char* activateLabel = "Activate";
    const char* writeLabel    = "Write...";
    const char* clearLabel    = "Unassign";
    const float actionsWidth = widgets::buttonWidth(writeLabel) + widgets::buttonWidth(clearLabel) + spacing +
                               (active ? 0.0f : widgets::buttonWidth(activateLabel) + spacing);
    ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - actionsWidth);
    ImGui::SetCursorPosY(rowTop);

    // Every button of the row starts at the row's top (SameLine alone would put them on the pills' baseline).
    const auto nextOnRow = [rowTop] {
        ImGui::SameLine();
        ImGui::SetCursorPosY(rowTop);
    };
    if (!active) {
        ImGui::BeginDisabled(!canAct);
        if (widgets::primaryButton(activateLabel, ImVec2(0, 0), p)) m_commands.activateSlot(static_cast<std::uint8_t>(slot));
        ImGui::EndDisabled();
        nextOnRow();
    }
    ImGui::BeginDisabled(!canAct || !info.hasProfile);
    if (widgets::secondaryButton(writeLabel, ImVec2(0, 0), p)) requestWrite(slot);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", info.hasProfile ? "Write this profile's settings to the keyboard..." : "This slot has no profile file to write.");
    }
    nextOnRow();
    ImGui::BeginDisabled(state.busy || !info.hasProfile);
    if (widgets::secondaryButton(clearLabel, ImVec2(0, 0), p)) m_commands.clearSlot(static_cast<std::uint8_t>(slot));
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Remove the file from this slot (the file stays in the library; the keyboard is not changed).");
    }

    // Row 2: the profile's name, with the file it comes from muted on the right when there is room for it.
    const char* name = info.hasProfile ? info.name.c_str() : "No profile assigned";
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.2f);
    if (!info.hasProfile) ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.textMuted));
    ImGui::TextUnformatted(name);
    if (!info.hasProfile) ImGui::PopStyleColor();
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
        const std::size_t overrides = info.storedKeys ? info.storedKeys->overrideCount() : 0;
        ImGui::TextWrapped("%s", describeTravel(info.storedSettings, overrides, extrasFor(state, info.fileName)).c_str());
    } else {
        ImGui::TextWrapped("Drop a profile from the library below onto this card to assign it.");
    }
    ImGui::PopStyleColor();

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    // Drop target: the whole card (EndChild made the card the last item).
    if (ImGui::BeginDragDropTarget()) {
        if (const ProfilePayload* dropped = acceptProfile()) {
            m_commands.assignProfileToSlot(dropped->fileName, static_cast<std::uint8_t>(slot), false);
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::PopID();
}

// -----------------------------------------------------------------------------
// Library
// -----------------------------------------------------------------------------
void ProfilesPanel::drawLibrary(const AppSnapshot& state, const Theme& theme) {
    const ThemePalette& p = theme.palette;

    widgets::sectionLabel("PROFILE LIBRARY", p);
    const char* openLabel    = "Open folder";
    const char* refreshLabel = "Refresh";
    ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - widgets::buttonWidth(openLabel) -
                    widgets::buttonWidth(refreshLabel) - ImGui::GetStyle().ItemSpacing.x);
    if (widgets::secondaryButton(openLabel, ImVec2(0, 0), p)) m_system.openProfilesFolder();
    ImGui::SameLine();
    if (widgets::secondaryButton(refreshLabel, ImVec2(0, 0), p)) m_commands.refresh();

    mutedText(p, "Every .json file in your profiles folder. Drag a card by its handle onto a slot to assign it, or "
                 "between cards to change the order the next / previous hotkeys cycle in (until the daemon restarts).");
    ImGui::Spacing();

    if (state.library.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.warning));
        ImGui::TextUnformatted("No .json profiles found in the profiles folder.");
        ImGui::PopStyleColor();
        return;
    }

    for (std::size_t i = 0; i < state.library.size(); ++i) {
        drawLibraryCard(state, state.library[i], i, theme);
        ImGui::Spacing();
    }
}

void ProfilesPanel::drawLibraryCard(const AppSnapshot& state, const LibraryProfileInfo& profile, const std::size_t index,
                                    const Theme& theme) {
    const ThemePalette& p = theme.palette;
    const bool assigned = profile.assignedSlot.has_value();
    const bool active   = assigned && state.activeSlot == static_cast<int>(*profile.assignedSlot);
    const bool canAct   = state.device.connected && !state.busy;

    ImGui::PushID(static_cast<int>(index));
    ImGui::PushStyleColor(ImGuiCol_Border, toImVec4(active ? p.accent : p.border));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toImVec4(p.surface));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::BeginChild("library_card", ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Row 1: drag handle, assignment pill, "Assign" menu on the right.
    widgets::secondaryButton("::##drag", ImVec2(0, 0), p);
    if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) ImGui::SetTooltip("Drag onto a slot to assign, or onto another card to reorder.");
    if (ImGui::BeginDragDropSource()) {
        ProfilePayload payload;
        payload.index = index;
        std::strncpy(payload.fileName, profile.fileName.c_str(), sizeof(payload.fileName) - 1);
        ImGui::SetDragDropPayload(PROFILE_PAYLOAD, &payload, sizeof(payload));
        ImGui::Text("%s", profile.name.c_str());
        ImGui::EndDragDropSource();
    }
    ImGui::SameLine();
    const float pillY = ImGui::GetCursorPosY() + (ImGui::GetFrameHeight() - widgets::pillHeight()) * 0.5f;
    ImGui::SetCursorPosY(pillY);
    if (assigned) {
        widgets::pill(upperSlotLabel(*profile.assignedSlot).c_str(), p.surfaceRaised, p.accentText);
    } else {
        widgets::pill("LIBRARY ONLY", p.surfaceRaised, p.textMuted);
    }

    const char* assignLabel = "Assign to...";
    ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - widgets::buttonWidth(assignLabel));
    ImGui::SetCursorPosY(pillY - (ImGui::GetFrameHeight() - widgets::pillHeight()) * 0.5f);
    ImGui::BeginDisabled(state.busy);
    if (widgets::secondaryButton(assignLabel, ImVec2(0, 0), p)) ImGui::OpenPopup("assign_menu");
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("assign_menu")) {
        ImGui::TextDisabled("'%s'", profile.name.c_str());
        ImGui::Separator();
        for (std::size_t s = 0; s < SLOT_COUNT; ++s) {
            const bool current = assigned && *profile.assignedSlot == s;
            const std::string item = slotLabel(s) + (state.slots[s].hasProfile && !current ? "  (replaces " + state.slots[s].name + ")" : "");
            if (ImGui::MenuItem(item.c_str(), nullptr, current, !current)) {
                m_commands.assignProfileToSlot(profile.fileName, static_cast<std::uint8_t>(s), false);
            }
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Assign and write to keyboard", canAct)) {
            for (std::size_t s = 0; s < SLOT_COUNT; ++s) {
                if (ImGui::MenuItem(slotLabel(s).c_str())) requestWrite(s, profile.fileName);
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    // Row 2: name and file.
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.12f);
    ImGui::TextUnformatted(profile.name.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.textMuted));
    ImGui::TextUnformatted(profile.fileName.c_str());

    // Row 3: settings.
    ImGui::TextWrapped("%s", describeTravel(profile.actuation, profile.keyOverrides, profile.extras).c_str());
    ImGui::PopStyleColor();

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    // Drop target: another library card dropped here takes this position.
    if (ImGui::BeginDragDropTarget()) {
        if (const ProfilePayload* dropped = acceptProfile()) {
            if (dropped->index != index) m_commands.reorderLibrary(dropped->index, index);
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::PopID();
}

// -----------------------------------------------------------------------------
// Confirmation
// -----------------------------------------------------------------------------
void ProfilesPanel::drawWriteConfirmation(const AppSnapshot& state, const Theme& theme) {
    const ThemePalette& p = theme.palette;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(viewport->WorkSize.x * 0.88f, 640.0f), 0.0f));
    if (!ImGui::BeginPopupModal(WRITE_POPUP_TITLE, nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize)) return;

    const std::size_t slot = std::min(m_writeSlot, SLOT_COUNT - 1);
    std::string name = state.slots[slot].name;
    if (!m_writeFileName.empty()) {
        const auto it = std::find_if(state.library.begin(), state.library.end(),
                                     [&](const LibraryProfileInfo& profile) { return profile.fileName == m_writeFileName; });
        name = it != state.library.end() ? it->name : m_writeFileName;
    }

    ImGui::PushTextWrapPos(0.0f);
    ImGui::Text("Write '%s' to %s?", name.c_str(), slotLabel(slot).c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    mutedText(p, "The profile's global actuation / Rapid Trigger settings, every key's own settings, gamepad, toggle and "
                 "dynamic-keystroke keys, SOCD pairs and its name are sent to the keyboard and saved to its flash, replacing "
                 "what this slot holds now. The result is read back and checked.");
    if (!m_writeFileName.empty() && state.slots[slot].hasProfile && state.slots[slot].fileName != m_writeFileName) {
        mutedText(p, ("'" + state.slots[slot].name + "' is unassigned from this slot (its file stays in the library).").c_str());
    }
    ImGui::Spacing();

    if (widgets::primaryButton("Write profile", ImVec2(0, 0), p)) {
        if (m_writeFileName.empty()) {
            m_commands.flashSlot(static_cast<std::uint8_t>(slot));
        } else {
            m_commands.assignProfileToSlot(m_writeFileName, static_cast<std::uint8_t>(slot), true);
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (widgets::secondaryButton("Cancel", ImVec2(0, 0), p) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace ui
