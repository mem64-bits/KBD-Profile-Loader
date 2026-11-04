#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "ui/imgui/ActuationPanel.h"
#include "ui/imgui/ImGuiTheme.h"
#include "ui/imgui/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace ui {

namespace {

    // "W, A, S, D" / "Q, W, E, R, T, Y +6 more"
    std::string selectionNames(const std::set<KeyPosition>& selection, const KeyboardLayout& layout) {
        constexpr std::size_t SHOWN = 8;
        std::string text;
        std::size_t listed = 0;
        for (const KeyGeometry& key : layout.keys) { // layout order reads naturally (row by row)
            if (!selection.contains(key.position())) continue;
            if (listed == SHOWN) break;
            if (listed > 0) text += ", ";
            text += widgets::keyName(key);
            ++listed;
        }
        if (selection.size() > listed) text += " +" + std::to_string(selection.size() - listed) + " more";
        return text;
    }

    constexpr KeyPosition MOVEMENT_KEYS[] = {{2, 2}, {3, 1}, {3, 2}, {3, 3}}; // W A S D (same on ANSI and ISO)

    kbd::model::TriggerSettings triggerSettings(const ActuationSettings& s) {
        return {s.actuationTenths, s.rapidTrigger, s.pressTenths, s.releaseTenths};
    }

    // Label on the left, value on the right, then a full-width slider.
    bool settingSlider(const char* label, const char* id, std::uint8_t& tenths, const float width, const ThemePalette& p,
                       const std::vector<float>& markers = {}) {
        widgets::labelWithValue(label, actuation::formatMillimetres(tenths), p);
        return widgets::millimetreSlider(id, tenths, actuation::MIN_TENTHS, actuation::MAX_TENTHS, width, p, markers);
    }

} // namespace

ActuationPanel::ActuationPanel(IProfileCommands& commands, std::shared_ptr<Workspace> workspace)
    : m_commands(commands), m_workspace(std::move(workspace)) {}

ActuationPanel::~ActuationPanel() = default;

const char* ActuationPanel::title() const noexcept {
    return "Actuation";
}

const char* ActuationPanel::subtitle() const noexcept {
    return "Where keys register, and Rapid Trigger, for the whole slot or key by key";
}

widgets::Icon ActuationPanel::icon() const noexcept {
    return widgets::Icon::Actuation;
}

void ActuationPanel::onHidden() {
    m_liveKey.reset();
    m_history.clear();
    m_commands.watchLiveTravel({});
}

ActuationSettings ActuationPanel::baselineFor(const AppSnapshot& state, const std::size_t slot) {
    if (const auto& known = state.actuation[slot]) return *known;
    if (const auto& stored = state.slots[slot].storedSettings) return *stored;
    return ActuationSettings{};
}

KeyMatrix ActuationPanel::keysFor(const AppSnapshot& state, const std::size_t slot) {
    if (const auto& known = state.keyMatrix[slot]) return *known;
    if (const auto& stored = state.slots[slot].storedKeys) return *stored;
    return KeyMatrix{};
}

ActuationPanel::Source ActuationPanel::keySource(const AppSnapshot& state, const std::size_t slot) {
    if (state.keyMatrix[slot] || state.actuation[slot]) return Source::Keyboard;
    if (state.slots[slot].storedKeys || state.slots[slot].storedSettings) return Source::ProfileFile;
    return Source::Defaults;
}

void ActuationPanel::updateLiveKey() {
    const std::optional<KeyPosition> wanted = m_selection.empty() ? std::nullopt : std::optional<KeyPosition>(*m_selection.begin());
    if (wanted != m_liveKey) {
        m_liveKey = wanted;
        m_history.clear();
        m_commands.watchLiveTravel(wanted ? std::vector<KeyPosition>{*wanted} : std::vector<KeyPosition>{});
    }
    if (!m_liveKey) return;
    for (const LiveKeyTravel& reading : m_commands.liveTravel()) {
        if (reading.key == *m_liveKey) {
            m_history.push(ImGui::GetTime(), static_cast<float>(reading.travelRaw) / static_cast<float>(kbd::model::TRAVEL_SCALE));
        }
    }
}

void ActuationPanel::render(const AppSnapshot& state, const Theme& theme) {
    const ThemePalette& p = theme.palette;
    const bool canAct = state.device.connected && !state.busy;
    const std::size_t slot = m_workspace->slot(state);

    if (slot != m_draftSlot) {
        m_keyDraft.edited = false;
        m_draftSlot = slot;
    }
    // Selecting keys switches the inspector to them; clearing the selection goes back to the slot.
    if (!m_selection.empty() && !m_hadSelection) m_tab = Tab::Keys;
    if (m_selection.empty() && m_hadSelection) m_tab = Tab::Slot;
    m_hadSelection = !m_selection.empty();

    updateLiveKey();
    plots::applyStyle(p);
    drawKeyboard(state, slot, p);
    ImGui::Spacing();
    drawInspector(state, slot, canAct, p);
}

// -----------------------------------------------------------------------------
// Keyboard view
// -----------------------------------------------------------------------------
void ActuationPanel::drawKeyboard(const AppSnapshot& state, const std::size_t slot, const ThemePalette& p) {
    const KeyboardLayout& layout = keyboardLayout(m_workspace->keyboardLayout(state));
    const ActuationSettings global = baselineFor(state, slot);
    const KeyMatrix keys = keysFor(state, slot);
    const float width = ImGui::GetContentRegionAvail().x;

    const auto display = [&](const KeyPosition& key) {
        const KeyActuation& own = keys.at(key);
        KeyDisplay shown;
        shown.effective   = actuation::effective(global, own);
        shown.ownSettings = !own.followsGlobal();
        if (m_liveKey && key == *m_liveKey && !m_history.empty()) shown.level = m_history.samples().back().tenths / 40.0f;
        return shown;
    };
    const KeyboardView::Result result = m_keyboard.draw("##keyboard", layout, display, m_selection, p, width);
    if (result.selectionChanged) m_keyDraft.edited = false;

    // One line under the board: the colour scale, what the marks mean, where the values come from.
    ImGui::Spacing();
    const float em = ImGui::GetFontSize();
    KeyboardView::drawHeatLegend(p, std::min(width * 0.42f, em * 22.0f));
    ImGui::SameLine(0.0f, em * 1.5f);
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float h = ImGui::GetTextLineHeight();
        const auto swatch = [&](const bool filled) {
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const ImVec2 size(h * 1.6f, h * 0.8f);
            const ImVec2 min(at.x, at.y + (h - size.y) * 0.5f);
            const Color color = KeyboardView::heatColor(5, p);
            if (filled) {
                draw->AddRectFilled(min, min + size, toImU32(color), size.y * 0.5f);
            } else {
                draw->AddRect(min, min + size, toImU32(color), size.y * 0.5f, 0, 1.0f);
            }
            ImGui::Dummy(ImVec2(size.x, h));
            ImGui::SameLine();
        };
        ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.textMuted));
        swatch(true);
        ImGui::TextUnformatted("own setting");
        ImGui::SameLine(0.0f, em);
        swatch(false);
        ImGui::TextUnformatted("follows the slot");
        ImGui::SameLine(0.0f, em);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        draw->AddCircleFilled(ImVec2(at.x + h * 0.3f, at.y + h * 0.5f), h * 0.22f, toImU32(p.success));
        ImGui::Dummy(ImVec2(h * 0.6f, h));
        ImGui::SameLine();
        ImGui::TextUnformatted("Rapid Trigger");
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    const char* source = "";
    Color sourceColor = p.textMuted;
    switch (keySource(state, slot)) {
        case Source::Keyboard:    source = "Read from the keyboard"; sourceColor = p.success; break;
        case Source::ProfileFile: source = "From the profile file (not read yet)"; sourceColor = p.warning; break;
        case Source::Defaults:    source = "Firmware defaults"; break;
    }
    widgets::alignRight(ImGui::CalcTextSize(source).x);
    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(sourceColor));
    ImGui::TextUnformatted(source);
    ImGui::PopStyleColor();
}

// -----------------------------------------------------------------------------
// Inspector: whole slot or selected keys, next to the diagram
// -----------------------------------------------------------------------------
void ActuationPanel::drawInspector(const AppSnapshot& state, const std::size_t slot, const bool canAct, const ThemePalette& p) {
    if (!widgets::beginCard("##inspector", p)) {
        widgets::endCard();
        return;
    }
    const float em = ImGui::GetFontSize();
    const KeyboardLayout& layout = keyboardLayout(m_workspace->keyboardLayout(state));

    // Header: what is being edited, quick selections.
    const std::string keysTab = m_selection.empty() ? std::string("Selected keys") : "Selected keys (" + std::to_string(m_selection.size()) + ")";
    const char* tabs[] = {"Whole slot", keysTab.c_str()};
    const int picked = widgets::segmented("##tab", tabs, static_cast<int>(m_tab), p, em * 18.0f);
    if (picked >= 0) m_tab = static_cast<Tab>(picked);

    const float quickWidth = widgets::buttonWidth("All") + widgets::buttonWidth("WASD") + widgets::buttonWidth("Own settings") +
                             widgets::buttonWidth("Clear") + ImGui::GetStyle().ItemSpacing.x * 3.0f;
    widgets::alignRight(quickWidth);
    const KeyMatrix keys = keysFor(state, slot);
    std::optional<std::set<KeyPosition>> pick;
    if (widgets::secondaryButton("All", ImVec2(0, 0), p)) {
        std::set<KeyPosition> all;
        for (const KeyGeometry& key : layout.keys) all.insert(key.position());
        pick = all;
    }
    ImGui::SameLine();
    if (widgets::secondaryButton("WASD", ImVec2(0, 0), p)) pick = std::set<KeyPosition>(std::begin(MOVEMENT_KEYS), std::end(MOVEMENT_KEYS));
    ImGui::SameLine();
    if (widgets::secondaryButton("Own settings", ImVec2(0, 0), p)) {
        std::set<KeyPosition> own;
        for (const KeyGeometry& key : layout.keys) {
            if (!keys.at(key.position()).followsGlobal()) own.insert(key.position());
        }
        pick = own;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select every key that has its own settings");
    ImGui::SameLine();
    if (widgets::secondaryButton("Clear", ImVec2(0, 0), p)) pick = std::set<KeyPosition>{};
    if (pick) {
        m_selection = *pick;
        m_keyDraft.edited = false;
        m_tab = m_selection.empty() ? Tab::Slot : Tab::Keys;
        m_hadSelection = !m_selection.empty();
    }
    ImGui::Spacing();

    // Two columns: controls, diagram.
    const float avail = ImGui::GetContentRegionAvail().x;
    const float controlsWidth = std::floor(std::max(em * 18.0f, avail * 0.42f));
    const float diagramWidth = avail - controlsWidth - em * 1.5f;
    ImGui::BeginGroup();
    ImGui::PushItemWidth(controlsWidth);
    ImGui::BeginChild("##controls", ImVec2(controlsWidth, 0), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar);
    const ActuationSettings shown = m_tab == Tab::Slot ? drawSlotControls(state, slot, canAct, p) : drawKeyControls(state, slot, canAct, p);
    ImGui::EndChild();
    ImGui::PopItemWidth();
    ImGui::EndGroup();
    const float controlsHeight = ImGui::GetItemRectSize().y;
    if (diagramWidth > em * 12.0f) {
        ImGui::SameLine(0.0f, em * 1.5f);
        drawDiagram(shown, diagramWidth, std::max(controlsHeight, em * 15.0f), p);
    }
    widgets::endCard();
}

ActuationSettings ActuationPanel::drawSlotControls(const AppSnapshot& state, const std::size_t slot, const bool canAct, const ThemePalette& p) {
    const float width = ImGui::GetContentRegionAvail().x;
    // Keep the draft in step with the keyboard unless the user has edits of their own.
    Draft& draft = m_drafts[slot];
    const ActuationSettings baseline = baselineFor(state, slot);
    const bool hadEdits = draft.initialised && draft.values != draft.baseline;
    if (!draft.initialised || (!hadEdits && draft.values != baseline)) draft.values = baseline;
    draft.baseline    = baseline;
    draft.initialised = true;
    const bool dirty = draft.values != baseline;

    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo("##preset", "Start from a preset...")) {
        for (const auto& preset : actuation::presets()) {
            const std::string entry = std::string(preset.name) + "   (" + actuation::describe(preset.settings) + ")";
            if (ImGui::Selectable(entry.c_str())) draft.values = preset.settings;
        }
        ImGui::EndCombo();
    }
    ImGui::Spacing();
    settingSlider("Actuation point", "##actuation", draft.values.actuationTenths, width, p,
                  dirty ? std::vector<float>{static_cast<float>(baseline.actuationTenths)} : std::vector<float>{});
    widgets::mutedWrapped("How far a key goes down before it registers. Smaller is more sensitive.", p);
    ImGui::Spacing();
    widgets::toggle("Rapid Trigger", draft.values.rapidTrigger, p);
    if (draft.values.rapidTrigger) {
        settingSlider("Press again after", "##press", draft.values.pressTenths, width, p);
        settingSlider("Release after", "##release", draft.values.releaseTenths, width, p);
        widgets::mutedWrapped("The key releases as soon as it moves up by the release distance and presses again as soon as it "
                              "moves down by the press distance, anywhere below the actuation point.", p);
    }
    ImGui::Spacing();

    ImGui::BeginDisabled(!canAct || !dirty);
    if (widgets::primaryButton("Apply and save##slot", ImVec2(0, 0), p)) m_commands.applyActuation(static_cast<std::uint8_t>(slot), draft.values, true);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Send to the keyboard and keep it after a replug");
    ImGui::SameLine();
    if (widgets::secondaryButton("Try##slot", ImVec2(0, 0), p)) m_commands.applyActuation(static_cast<std::uint8_t>(slot), draft.values, false);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Send to the keyboard without saving (lost on replug)");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!dirty);
    if (widgets::secondaryButton("Revert##slot", ImVec2(0, 0), p)) draft.values = baseline;
    ImGui::EndDisabled();

    const std::size_t overrides = keysFor(state, slot).overrideCount();
    std::string note = overrides == 0 ? "Every key follows these settings." :
                                        std::to_string(overrides) + (overrides == 1 ? " key keeps its own settings." : " keys keep their own settings.");
    if (dirty) note = "Unapplied changes. " + note;
    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(dirty ? p.warning : p.textMuted));
    ImGui::TextWrapped("%s", note.c_str());
    ImGui::PopStyleColor();
    return draft.values;
}

void ActuationPanel::syncKeyDraft(const AppSnapshot& state, const std::size_t slot) {
    if (m_keyDraft.edited || m_selection.empty()) return;
    const KeyActuation key = keysFor(state, slot).at(*m_selection.begin());
    m_keyDraft.ownSettings = !key.followsGlobal();
    m_keyDraft.values      = actuation::effective(baselineFor(state, slot), key);
}

ActuationSettings ActuationPanel::drawKeyControls(const AppSnapshot& state, const std::size_t slot, const bool canAct, const ThemePalette& p) {
    const float width = ImGui::GetContentRegionAvail().x;
    const KeyboardLayout& layout = keyboardLayout(m_workspace->keyboardLayout(state));
    const ActuationSettings global = baselineFor(state, slot);
    const KeyMatrix keys = keysFor(state, slot);
    syncKeyDraft(state, slot);

    if (m_selection.empty()) {
        widgets::mutedWrapped("No keys selected. Click keys on the board (Ctrl+click adds, drag selects an area, double-click picks "
                              "every key with the same settings) to give them their own actuation point or Rapid Trigger.", p);
        return global;
    }

    ImGui::TextWrapped("%s", selectionNames(m_selection, layout).c_str());
    const KeyActuation first = keys.at(*m_selection.begin());
    const bool same = std::all_of(m_selection.begin(), m_selection.end(), [&](const KeyPosition& k) { return keys.at(k) == first; });
    widgets::mutedWrapped(same ? ("Now: " + actuation::describe(first, global) + ".").c_str()
                               : "These keys have different settings; applying gives them all the values below.",
                          p);
    ImGui::Spacing();

    static constexpr const char* MODES[] = {"Follow the slot", "Own settings"};
    const int mode = widgets::segmented("##keymode", MODES, m_keyDraft.ownSettings ? 1 : 0, p, width);
    if (mode >= 0) {
        m_keyDraft.ownSettings = mode == 1;
        m_keyDraft.edited = true;
    }
    ImGui::Spacing();
    if (m_keyDraft.ownSettings) {
        ActuationSettings& v = m_keyDraft.values;
        if (settingSlider("Actuation point", "##key_actuation", v.actuationTenths, width, p, {static_cast<float>(global.actuationTenths)})) {
            m_keyDraft.edited = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The yellow mark is the slot's actuation point");
        if (widgets::toggle("Rapid Trigger##key", v.rapidTrigger, p)) m_keyDraft.edited = true;
        if (v.rapidTrigger) {
            if (settingSlider("Press again after", "##key_press", v.pressTenths, width, p)) m_keyDraft.edited = true;
            if (settingSlider("Release after", "##key_release", v.releaseTenths, width, p)) m_keyDraft.edited = true;
        }
    } else {
        widgets::mutedWrapped(("The keys use the slot's settings: " + actuation::describe(global) + ".").c_str(), p);
    }
    ImGui::Spacing();

    const KeyActuation target = m_keyDraft.ownSettings ? actuation::overrideFrom(m_keyDraft.values) : KeyActuation{};
    const bool dirty = std::any_of(m_selection.begin(), m_selection.end(), [&](const KeyPosition& k) { return keys.at(k) != target; });
    const std::string applyLabel = "Apply and save to " + std::to_string(m_selection.size()) + (m_selection.size() == 1 ? " key" : " keys");
    ImGui::BeginDisabled(!canAct || !dirty);
    const bool applyAndSave = widgets::primaryButton(applyLabel.c_str(), ImVec2(0, 0), p);
    ImGui::SameLine();
    const bool apply = widgets::secondaryButton("Try##keys", ImVec2(0, 0), p);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Send to the keyboard without saving (lost on replug)");
    ImGui::EndDisabled();
    if (apply || applyAndSave) {
        m_commands.applyKeyActuation(static_cast<std::uint8_t>(slot), std::vector<KeyPosition>(m_selection.begin(), m_selection.end()),
                                     target, applyAndSave);
        m_keyDraft.edited = false;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_keyDraft.edited);
    if (widgets::secondaryButton("Revert##keys", ImVec2(0, 0), p)) m_keyDraft.edited = false;
    ImGui::EndDisabled();
    if (dirty && m_keyDraft.edited) {
        ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.warning));
        ImGui::TextUnformatted("Unapplied changes.");
        ImGui::PopStyleColor();
    }
    return m_keyDraft.ownSettings ? m_keyDraft.values : global;
}

void ActuationPanel::drawDiagram(const ActuationSettings& settings, const float width, const float height, const ThemePalette& p) {
    ImGui::BeginGroup();
    const bool live = m_liveKey && m_history.samples().size() > 2;
    const double now = ImGui::GetTime();
    plots::TravelPlotOptions options;
    options.settings = triggerSettings(settings);
    options.live = live;
    const float captionHeight = ImGui::GetTextLineHeightWithSpacing() * 2.2f;
    if (live) {
        const std::vector<plots::TravelSample> samples(m_history.samples().begin(), m_history.samples().end());
        plots::travelPlot("##travel", samples, now, options, ImVec2(width, height - captionHeight), p);
    } else {
        const std::vector<plots::TravelSample> samples = plots::demoPattern(now, options.window + 1.0);
        plots::travelPlot("##travel", samples, now, options, ImVec2(width, height - captionHeight), p);
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    widgets::mutedWrapped(live ? "Live: press the selected key. Shaded where the keyboard reports it as pressed with these settings."
                               : "Example press. Select a key and press it to see your own; shaded where the keyboard would report it "
                                 "as pressed with these settings.",
                          p);
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
}

} // namespace ui
