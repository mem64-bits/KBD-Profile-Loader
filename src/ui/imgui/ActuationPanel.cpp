#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "ui/imgui/ActuationPanel.h"
#include "ui/imgui/ImGuiTheme.h"
#include "ui/imgui/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace ui {

namespace {

    void mutedWrapped(const ThemePalette& palette, const char* text) {
        ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(palette.textMuted));
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    }

} // namespace

ActuationPanel::ActuationPanel(IProfileCommands& commands) : m_commands(commands) {}

ActuationPanel::~ActuationPanel() = default;

const char* ActuationPanel::title() const noexcept {
    return "Actuation";
}

ActuationSettings ActuationPanel::baselineFor(const AppSnapshot& state, const std::size_t slot) {
    if (const auto& known = state.actuation[slot]) return *known;
    if (const auto& stored = state.slots[slot].storedSettings) return *stored;
    return ActuationSettings{};
}

void ActuationPanel::render(const AppSnapshot& state, const Theme& theme) {
    const ThemePalette& p = theme.palette;
    const bool canAct = state.device.connected && !state.busy;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;

    const std::size_t slot = m_pickedSlot ? *m_pickedSlot : static_cast<std::size_t>(std::max(state.activeSlot, 0));

    // --- keep the draft in step with the keyboard unless the user has edits of their own ---
    Draft& draft = m_drafts[slot];
    const ActuationSettings baseline = baselineFor(state, slot);
    const bool hadEdits = draft.initialised && draft.values != draft.baseline;
    if (!draft.initialised || (!hadEdits && draft.values != baseline)) draft.values = baseline;
    draft.baseline    = baseline;
    draft.initialised = true;
    const bool dirty = draft.values != baseline;

    // --- slot selector ---
    widgets::sectionLabel("PROFILE SLOT", p);
    const float slotWidth = (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f;
    for (std::size_t i = 0; i < SLOT_COUNT; ++i) {
        if (i > 0) ImGui::SameLine();
        ImGui::PushID(static_cast<int>(i));
        const std::string label = slotLabel(i) + (state.activeSlot == static_cast<int>(i) ? " *" : "");
        const bool pressed = (i == slot) ? widgets::primaryButton(label.c_str(), ImVec2(slotWidth, 0), p)
                                         : widgets::secondaryButton(label.c_str(), ImVec2(slotWidth, 0), p);
        if (pressed) m_pickedSlot = i;
        ImGui::PopID();
    }
    mutedWrapped(p, ("* = active on the keyboard.   Editing: " +
                     (state.slots[slot].hasProfile ? state.slots[slot].name : slotLabel(slot))).c_str());
    ImGui::Spacing();

    // --- presets + read-back ---
    const float readWidth = ImGui::CalcTextSize("Read from keyboard").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - readWidth - spacing);
    if (ImGui::BeginCombo("##preset", "Presets")) {
        for (const auto& preset : actuation::presets()) {
            const std::string entry = std::string(preset.name) + "   (" + actuation::describe(preset.settings) + ")";
            if (ImGui::Selectable(entry.c_str())) draft.values = preset.settings;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!canAct);
    if (widgets::secondaryButton("Read from keyboard", ImVec2(0, 0), p)) m_commands.readActuation(static_cast<std::uint8_t>(slot));
    ImGui::EndDisabled();
    ImGui::Spacing();

    // --- actuation point ---
    const float width = ImGui::GetContentRegionAvail().x;
    widgets::labelWithValue("ACTUATION POINT", actuation::formatMillimetres(draft.values.actuationTenths), p);
    widgets::millimetreSlider("##actuation", draft.values.actuationTenths, actuation::MIN_TENTHS, actuation::MAX_TENTHS, width);
    ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.4f));
    widgets::travelDiagram(width, draft.values.actuationTenths, p);
    mutedWrapped(p, "How far the key has to travel before it registers. Smaller = more sensitive.");
    ImGui::Spacing();

    // --- rapid trigger ---
    ImGui::Checkbox("Rapid Trigger", &draft.values.rapidTrigger);
    if (draft.values.rapidTrigger) {
        widgets::labelWithValue("PRESS SENSITIVITY", actuation::formatMillimetres(draft.values.pressTenths), p);
        widgets::millimetreSlider("##press", draft.values.pressTenths, actuation::MIN_TENTHS, actuation::MAX_TENTHS, width);
        widgets::labelWithValue("RELEASE SENSITIVITY", actuation::formatMillimetres(draft.values.releaseTenths), p);
        widgets::millimetreSlider("##release", draft.values.releaseTenths, actuation::MIN_TENTHS, actuation::MAX_TENTHS, width);
        mutedWrapped(p, "The key re-actuates as soon as it moves down by the press distance, and releases as soon as "
                        "it moves up by the release distance.");
    } else {
        mutedWrapped(p, "Off: the key registers at the actuation point and releases when it passes back above it.");
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- apply ---
    ImGui::BeginDisabled(!canAct);
    if (widgets::primaryButton("Apply", ImVec2(0, 0), p)) {
        m_commands.applyActuation(static_cast<std::uint8_t>(slot), draft.values, false);
    }
    ImGui::SameLine();
    if (widgets::primaryButton("Apply and save to keyboard", ImVec2(0, 0), p)) {
        m_commands.applyActuation(static_cast<std::uint8_t>(slot), draft.values, true);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!dirty);
    if (widgets::secondaryButton("Revert", ImVec2(0, 0), p)) draft.values = baseline;
    ImGui::EndDisabled();

    if (dirty) widgets::pill("UNAPPLIED CHANGES", p.surfaceRaised, p.warning);
    mutedWrapped(p, "Settings apply to every key of this slot. \"Apply\" changes the running profile; saving also "
                    "commits it to the keyboard's flash.");
}

} // namespace ui
