#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "ui/imgui/MovementPanel.h"
#include "ui/ControllerMapping.h"
#include "ui/imgui/ImGuiTheme.h"
#include "ui/imgui/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace ui {

namespace {

    namespace SocdType = kbd::model::SocdType;
    constexpr std::size_t MAX_WATCHED = 4;
    constexpr double BENCH_WINDOW = 4.0;

    // Distinct colours for the SOCD pairs, taken from the theme so they follow it.
    Color pairColor(const std::size_t index, const ThemePalette& p) {
        const Color colors[] = {p.accentText, p.success, p.warning, Color{0x38, 0xBD, 0xF8}, Color{0xF4, 0x72, 0xB6}};
        return colors[index % std::size(colors)];
    }
    const Color KEY2_COLOR{0x38, 0xBD, 0xF8};

    std::string millimetres(const std::uint8_t tenths) {
        char text[16];
        std::snprintf(text, sizeof(text), "%.1f mm", static_cast<double>(tenths) / 10.0);
        return text;
    }

    std::string pairName(const kbd::SocdPair& pair, const LayoutKind layout) {
        return widgets::keyName(KeyPosition{pair.row1, pair.col1}, layout) + " + " + widgets::keyName(KeyPosition{pair.row2, pair.col2}, layout);
    }

    std::string describeCombo(const kbd::OkmcConfig& okmc) {
        if (!mapping::isDeepPressCombo(okmc)) return "Dynamic keystroke (edited in the Keychron Launcher)";
        return mapping::keycodeName(okmc.keycodes[0]) + " from " + millimetres(okmc.shallowActTenths) + ", + " +
               mapping::keycodeName(okmc.keycodes[1]) + " from " + millimetres(okmc.deepActTenths);
    }

    ActuationSettings effectiveFor(const AppSnapshot& state, const std::size_t slot, const KeyPosition& key) {
        ActuationSettings global = state.actuation[slot].value_or(state.slots[slot].storedSettings.value_or(ActuationSettings{}));
        const KeyMatrix keys = state.keyMatrix[slot].value_or(state.slots[slot].storedKeys.value_or(KeyMatrix{}));
        return actuation::effective(global, keys.at(key));
    }

    // 0 - 4 mm bar with the walk and run zones, and where the key is right now.
    void sprintDiagram(const float width, const std::uint8_t shallow, const std::uint8_t deep, const std::optional<float> live,
                       const ThemePalette& p) {
        const float em = ImGui::GetFontSize();
        const float height = em * 1.4f;
        const ImVec2 origin = ImGui::GetCursorScreenPos() + ImVec2(0, em * 1.25f);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const auto xAt = [&](const float tenths) { return origin.x + width * std::clamp(tenths / 40.0f, 0.0f, 1.0f); };
        const float bottom = origin.y + height;
        const float rounding = height * 0.35f;

        draw->AddRectFilled(origin, ImVec2(origin.x + width, bottom), toImU32(p.surfaceRaised), rounding);
        draw->AddRectFilled(ImVec2(xAt(shallow), origin.y), ImVec2(xAt(deep), bottom), toImU32(p.accent.withAlpha(80)));
        draw->AddRectFilledMultiColor(ImVec2(xAt(deep), origin.y), ImVec2(origin.x + width - rounding, bottom), toImU32(p.accent.withAlpha(150)),
                                      toImU32(p.accent), toImU32(p.accent), toImU32(p.accent.withAlpha(150)));
        draw->AddRectFilled(ImVec2(origin.x + width - rounding * 2.0f, origin.y), ImVec2(origin.x + width, bottom), toImU32(p.accent), rounding,
                            ImDrawFlags_RoundCornersRight);
        if (live) {
            const float x = xAt(*live);
            draw->AddRectFilled(origin, ImVec2(x, bottom), toImU32(p.text.withAlpha(40)), rounding, ImDrawFlags_RoundCornersLeft);
            draw->AddLine(ImVec2(x, origin.y - 3.0f), ImVec2(x, bottom + 3.0f), toImU32(p.success), 2.5f);
        }
        draw->AddRect(origin, ImVec2(origin.x + width, bottom), toImU32(p.border), rounding);

        const auto label = [&](const float x, const char* text, const Color& color) {
            const ImVec2 size = ImGui::CalcTextSize(text);
            draw->AddText(ImVec2(std::clamp(x - size.x * 0.5f, origin.x, origin.x + width - size.x), origin.y - em * 1.2f), toImU32(color), text);
        };
        label((origin.x + xAt(shallow)) * 0.5f, "nothing", p.textMuted);
        label((xAt(shallow) + xAt(deep)) * 0.5f, "walk", p.text);
        label((xAt(deep) + origin.x + width) * 0.5f, "run", p.accentText);
        for (int mm = 0; mm <= 4; ++mm) {
            const std::string text = mm == 4 ? "4 mm" : std::to_string(mm);
            const ImVec2 size = ImGui::CalcTextSize(text.c_str());
            const float x = xAt(static_cast<float>(mm * 10));
            draw->AddText(ImVec2(std::clamp(x - size.x * 0.5f, origin.x, origin.x + width - size.x), bottom + em * 0.25f), toImU32(p.textMuted),
                          text.c_str());
        }
        ImGui::Dummy(ImVec2(width, height + em * 2.6f));
    }

} // namespace

MovementPanel::MovementPanel(IProfileCommands& commands, std::shared_ptr<Workspace> workspace)
    : m_commands(commands), m_workspace(std::move(workspace)) {}

const char* MovementPanel::title() const noexcept {
    return "Movement";
}

const char* MovementPanel::subtitle() const noexcept {
    return "SOCD (Snap Tap) for opposite keys, and sprint on a deep press";
}

widgets::Icon MovementPanel::icon() const noexcept {
    return widgets::Icon::Movement;
}

void MovementPanel::onHidden() {
    m_watching.clear();
    m_commands.watchLiveTravel({});
}

void MovementPanel::updateWatch() {
    std::vector<KeyPosition> wanted;
    // The SOCD pair on the test bench first, then the selection.
    if (m_benchPair < m_socdDraft.size() && !m_simulate) {
        const kbd::SocdPair& pair = m_socdDraft[m_benchPair];
        wanted.push_back({pair.row1, pair.col1});
        wanted.push_back({pair.row2, pair.col2});
    }
    for (const KeyPosition& key : m_selection) {
        if (wanted.size() == MAX_WATCHED) break;
        if (std::find(wanted.begin(), wanted.end(), key) == wanted.end()) wanted.push_back(key);
    }
    if (wanted != m_watching) {
        m_watching = wanted;
        m_commands.watchLiveTravel(wanted);
    }
    m_live = m_commands.liveTravel();
}

std::optional<float> MovementPanel::liveTenths(const KeyPosition& key) const noexcept {
    for (const LiveKeyTravel& reading : m_live) {
        if (reading.key == key) return static_cast<float>(reading.travelRaw) / static_cast<float>(kbd::model::TRAVEL_SCALE);
    }
    return std::nullopt;
}

int MovementPanel::socdPairOf(const KeyPosition& key) const noexcept {
    for (std::size_t i = 0; i < m_socdDraft.size(); ++i) {
        const kbd::SocdPair& pair = m_socdDraft[i];
        if ((pair.row1 == key.row && pair.col1 == key.col) || (pair.row2 == key.row && pair.col2 == key.col)) return static_cast<int>(i);
    }
    return -1;
}

bool MovementPanel::canAddPair(const KeyPosition& a, const KeyPosition& b) const noexcept {
    if (a == b || m_socdDraft.size() >= mapping::SOCD_PAIR_LIMIT) return false;
    return std::none_of(m_socdDraft.begin(), m_socdDraft.end(), [&](const kbd::SocdPair& pair) {
        const KeyPosition p1{pair.row1, pair.col1};
        const KeyPosition p2{pair.row2, pair.col2};
        return (p1 == a && p2 == b) || (p1 == b && p2 == a);
    });
}

void MovementPanel::render(const AppSnapshot& state, const Theme& theme) {
    const ThemePalette& p = theme.palette;
    const bool canAct = state.device.connected && !state.busy;
    const std::size_t slot = m_workspace->slot(state);
    const LayoutKind layout = m_workspace->keyboardLayout(state);

    const SlotDetails details = state.details[slot].value_or(SlotDetails{});
    if (slot != m_draftSlot || !m_socdEdited) {
        m_socdDraft  = details.socdPairs();
        if (m_socdDraft.size() > mapping::SOCD_PAIR_LIMIT) m_socdDraft.resize(mapping::SOCD_PAIR_LIMIT);
        m_socdEdited = false;
        m_draftSlot  = slot;
    }
    if (!state.device.connected) m_simulate = true;
    if (m_benchPair >= m_socdDraft.size()) m_benchPair = 0;
    updateWatch();

    drawKeyboard(details, layout, p);
    ImGui::Spacing();
    drawSocd(state, slot, canAct, layout, p);
    ImGui::Spacing();
    drawSprint(slot, canAct, layout, details, p);
}

void MovementPanel::drawKeyboard(const SlotDetails& details, const LayoutKind layout, const ThemePalette& p) {
    const auto display = [&](const KeyPosition& key) {
        KeyDisplay shown;
        shown.hideValue = true;
        if (const auto depth = liveTenths(key)) shown.level = *depth / 40.0f;
        if (const int pair = socdPairOf(key); pair >= 0) {
            const kbd::SocdPair& socd = m_socdDraft[static_cast<std::size_t>(pair)];
            const bool first = socd.row1 == key.row && socd.col1 == key.col;
            const Color color = pairColor(static_cast<std::size_t>(pair), p);
            shown.badge = KeyBadge{first ? "1" : "2", KeyGlyph::None, color, p.background, std::nullopt, gamepad::LabelStyle::Xbox, std::nullopt, false};
            shown.tint  = color;
            for (std::size_t i = 0; i < m_socdDraft.size(); ++i) {
                const kbd::SocdPair& other = m_socdDraft[i];
                if (!((other.row1 == key.row && other.col1 == key.col) || (other.row2 == key.row && other.col2 == key.col))) continue;
                const mapping::SocdMode* mode = mapping::socdMode(other.type);
                if (!shown.note.empty()) shown.note += "\n";
                shown.note += "SOCD pair " + std::to_string(i + 1) + ": " + pairName(other, layout) + " - " + (mode ? mode->name : "?");
            }
            return shown;
        }
        const KeyAdvanced advanced = details.at(key);
        if (advanced.mode == kbd::AdvancedMode::DKS && advanced.data < details.okmc.size()) {
            const kbd::OkmcConfig& okmc = details.okmc[advanced.data];
            shown.badge = KeyBadge{mapping::isDeepPressCombo(okmc) ? "RUN" : "DKS", KeyGlyph::None, p.accent, p.textOnAccent, std::nullopt,
                                   gamepad::LabelStyle::Xbox, std::nullopt, false};
            shown.tint  = p.accent;
            shown.note  = describeCombo(okmc);
        } else if (advanced.mode == kbd::AdvancedMode::GAMEPAD) {
            shown.dim  = true;
            shown.note = "Gamepad key (" + mapping::targetName(advanced.data, gamepad::LabelStyle::Xbox) + "): see the Gamepad page.";
        }
        return shown;
    };
    m_keyboard.draw("##movement_keys", keyboardLayout(layout), display, m_selection, p, ImGui::GetContentRegionAvail().x);
}

// -----------------------------------------------------------------------------
// SOCD
// -----------------------------------------------------------------------------
void MovementPanel::drawSocd(const AppSnapshot& state, const std::size_t slot, const bool canAct, const LayoutKind layout, const ThemePalette& p) {
    if (!widgets::beginCard("##socd", p)) {
        widgets::endCard();
        return;
    }
    const float em = ImGui::GetFontSize();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.12f);
    ImGui::TextUnformatted("SOCD  (Snap Tap)");
    ImGui::PopFont();
    const std::string count = std::to_string(m_socdDraft.size()) + " of " + std::to_string(mapping::SOCD_PAIR_LIMIT) + " pairs";
    ImGui::SameLine();
    widgets::alignRight(ImGui::CalcTextSize(count.c_str()).x);
    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.textMuted));
    ImGui::TextUnformatted(count.c_str());
    ImGui::PopStyleColor();
    widgets::mutedWrapped("What the keyboard sends while both keys of a pair are held, e.g. A and D while strafing. The firmware "
                          "decides, so it works in every game.", p);
    ImGui::Spacing();

    drawSocdPairs(layout, p);

    // Adding pairs.
    ImGui::Spacing();
    const auto addPair = [&](const KeyPosition a, const KeyPosition b) {
        m_socdDraft.push_back(kbd::SocdPair{a.row, a.col, b.row, b.col, SocdType::LAST_KEYSTROKE}); // the usual choice
        m_socdEdited = true;
        m_benchPair  = m_socdDraft.size() - 1;
    };
    const bool twoSelected = m_selection.size() == 2;
    const KeyPosition firstKey  = twoSelected ? *m_selection.begin() : KeyPosition{};
    const KeyPosition secondKey = twoSelected ? *std::next(m_selection.begin()) : KeyPosition{};
    const std::string addLabel = twoSelected ? "Add " + widgets::keyName(firstKey, layout) + " + " + widgets::keyName(secondKey, layout)
                                             : std::string("Add selected pair");
    ImGui::BeginDisabled(!twoSelected || !canAddPair(firstKey, secondKey));
    if (widgets::secondaryButton(addLabel.c_str(), ImVec2(0, 0), p)) addPair(firstKey, secondKey);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Select exactly two keys on the keyboard first.");
    struct Quick {
        const char* label;
        KeyPosition a;
        KeyPosition b;
    };
    const Quick quick[] = {{"A + D", {3, 1}, {3, 3}}, {"W + S", {2, 2}, {3, 2}}, {"Left + Right", {5, 12}, {5, 14}}};
    for (const Quick& q : quick) {
        ImGui::SameLine();
        ImGui::BeginDisabled(!canAddPair(q.a, q.b));
        if (widgets::secondaryButton(q.label, ImVec2(0, 0), p)) addPair(q.a, q.b);
        ImGui::EndDisabled();
    }

    // Apply.
    ImGui::SameLine();
    const float applyWidth = widgets::buttonWidth("Apply and save") + widgets::buttonWidth("Try") + widgets::buttonWidth("Revert") +
                             ImGui::GetStyle().ItemSpacing.x * 2.0f;
    widgets::alignRight(applyWidth);
    ImGui::BeginDisabled(!canAct || !m_socdEdited);
    if (widgets::primaryButton("Apply and save", ImVec2(0, 0), p)) {
        m_commands.applySocdPairs(static_cast<std::uint8_t>(slot), m_socdDraft, true);
        m_socdEdited = false;
    }
    ImGui::SameLine();
    if (widgets::secondaryButton("Try", ImVec2(0, 0), p)) {
        m_commands.applySocdPairs(static_cast<std::uint8_t>(slot), m_socdDraft, false);
        m_socdEdited = false;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Send to the keyboard without saving (lost on replug)");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_socdEdited);
    if (widgets::secondaryButton("Revert", ImVec2(0, 0), p)) m_socdEdited = false;
    ImGui::EndDisabled();
    if (m_socdEdited) {
        ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.warning));
        ImGui::TextUnformatted("Unapplied changes: the test bench already shows them.");
        ImGui::PopStyleColor();
    }

    // Test bench.
    if (!m_socdDraft.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        drawBench(state, slot, layout, ImGui::GetContentRegionAvail().x, p);
    }
    (void)em;
    widgets::endCard();
}

void MovementPanel::drawSocdPairs(const LayoutKind layout, const ThemePalette& p) {
    const float em = ImGui::GetFontSize();
    std::optional<std::size_t> removeAt;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (std::size_t i = 0; i < m_socdDraft.size(); ++i) {
        kbd::SocdPair& pair = m_socdDraft[i];
        ImGui::PushID(static_cast<int>(i));
        const ImVec2 rowMin = ImGui::GetCursorScreenPos();
        const float rowH = ImGui::GetFrameHeight() + em * 0.5f;
        const bool onBench = i == m_benchPair;
        draw->AddRectFilled(rowMin, ImVec2(rowMin.x + ImGui::GetContentRegionAvail().x, rowMin.y + rowH),
                            toImU32(onBench ? p.accent.withAlpha(28) : p.surfaceRaised.withAlpha(90)), em * 0.4f);
        ImGui::SetCursorScreenPos(rowMin + ImVec2(em * 0.5f, em * 0.25f));

        // Colour + keys; clicking the name puts the pair on the test bench.
        const ImVec2 at = ImGui::GetCursorScreenPos();
        draw->AddCircleFilled(ImVec2(at.x + em * 0.35f, at.y + ImGui::GetFrameHeight() * 0.5f), em * 0.32f, toImU32(pairColor(i, p)));
        ImGui::Dummy(ImVec2(em * 0.9f, ImGui::GetFrameHeight()));
        ImGui::SameLine();
        const std::string name = pairName(pair, layout);
        ImGui::AlignTextToFramePadding();
        if (ImGui::Selectable((name + "##pair").c_str(), onBench, 0, ImVec2(em * 7.0f, 0))) m_benchPair = i;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show this pair on the test bench");

        // Mode: a picker named like the Keychron Launcher, with what it does next to it.
        ImGui::SameLine(em * 9.5f);
        const std::string key1 = widgets::keyName(KeyPosition{pair.row1, pair.col1}, layout);
        const std::string key2 = widgets::keyName(KeyPosition{pair.row2, pair.col2}, layout);
        const auto label = [&](const std::uint8_t type) -> std::string {
            switch (type) {
                case SocdType::NEUTRAL:              return "Neutral";
                case SocdType::LAST_KEYSTROKE:       return "Last Key Priority";
                case SocdType::DEEPER_TRAVEL:        return "Rappy Snappy";
                case SocdType::DEEPER_TRAVEL_SINGLE: return "Rappy Snappy (hold winner)";
                case SocdType::KEY_1:                return "Absolute Priority: " + key1;
                case SocdType::KEY_2:                return "Absolute Priority: " + key2;
                default:                             return "Off";
            }
        };
        const std::uint8_t order[] = {SocdType::NEUTRAL, SocdType::LAST_KEYSTROKE, SocdType::DEEPER_TRAVEL,
                                      SocdType::DEEPER_TRAVEL_SINGLE, SocdType::KEY_1, SocdType::KEY_2};
        ImGui::SetNextItemWidth(em * 15.0f);
        if (ImGui::BeginCombo("##mode", label(pair.type).c_str())) {
            for (const std::uint8_t type : order) {
                if (ImGui::Selectable(label(type).c_str(), type == pair.type) && type != pair.type) {
                    pair.type = type;
                    m_socdEdited = true;
                    m_benchPair = i;
                }
                if (ImGui::IsItemHovered()) {
                    if (const mapping::SocdMode* mode = mapping::socdMode(type)) ImGui::SetTooltip("%s", mode->description);
                }
            }
            ImGui::EndCombo();
        }
        // One line on what the mode does, cut to the room left (full text in the tooltip).
        if (const mapping::SocdMode* mode = mapping::socdMode(pair.type)) {
            ImGui::SameLine(0.0f, em * 0.8f);
            const float room = ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - em;
            std::string text = pair.type == SocdType::NEUTRAL ? "Both held: neither key is sent." : mode->description;
            const std::string full = text;
            while (!text.empty() && ImGui::CalcTextSize((text + "...").c_str()).x > room) text.pop_back();
            if (text.size() < full.size()) text += "...";
            ImGui::AlignTextToFramePadding();
            ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.textMuted));
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && text.size() < full.size()) ImGui::SetTooltip("%s", full.c_str());
        }
        widgets::alignRight(ImGui::GetFrameHeight());
        if (widgets::secondaryButton("x", ImVec2(ImGui::GetFrameHeight(), 0), p)) removeAt = i;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this pair");
        ImGui::SetCursorScreenPos(ImVec2(rowMin.x, rowMin.y + rowH + em * 0.3f));
        ImGui::Dummy(ImVec2(0, 0));
        ImGui::PopID();
    }
    if (removeAt) {
        m_socdDraft.erase(m_socdDraft.begin() + static_cast<std::ptrdiff_t>(*removeAt));
        m_socdEdited = true;
    }
    if (m_socdDraft.empty()) widgets::mutedWrapped("No pairs in this slot yet: add A + D for strafing, or select two keys.", p);
}

void MovementPanel::drawBench(const AppSnapshot& state, const std::size_t slot, const LayoutKind layout, const float width, const ThemePalette& p) {
    const float em = ImGui::GetFontSize();
    const kbd::SocdPair pair = m_socdDraft[m_benchPair];
    const KeyPosition k1{pair.row1, pair.col1};
    const KeyPosition k2{pair.row2, pair.col2};
    const std::string name1 = widgets::keyName(k1, layout);
    const std::string name2 = widgets::keyName(k2, layout);
    if (!m_benchFor || m_benchFor->row1 != pair.row1 || m_benchFor->col1 != pair.col1 || m_benchFor->row2 != pair.row2 || m_benchFor->col2 != pair.col2) {
        m_socdModel.reset();
        for (auto& key : m_benchKeys) key.reset();
        m_bench.clear();
        m_benchFor = pair;
    }

    // Header: title, mode in words, live / simulate switch.
    ImGui::TextUnformatted(("Test bench: " + name1 + " + " + name2).c_str());
    ImGui::SameLine();
    const bool connected = state.device.connected;
    widgets::alignRight(em * 9.0f);
    ImGui::BeginDisabled(!connected);
    bool simulate = m_simulate;
    if (widgets::toggle("Simulate##bench", simulate, p)) m_simulate = simulate;
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(connected ? "On: drag the two keys below. Off: press the real keys." : "No keyboard: drag the two keys below.");
    }
    if (const mapping::SocdMode* mode = mapping::socdMode(pair.type)) widgets::mutedWrapped(mode->description, p);

    // Inputs: live travel or the simulated depths.
    std::array<float, 2> depth{};
    if (m_simulate) {
        depth = m_simTenths;
    } else {
        depth[0] = liveTenths(k1).value_or(0.0f);
        depth[1] = liveTenths(k2).value_or(0.0f);
    }
    const kbd::model::TriggerSettings s1{effectiveFor(state, slot, k1).actuationTenths, effectiveFor(state, slot, k1).rapidTrigger,
                                         effectiveFor(state, slot, k1).pressTenths, effectiveFor(state, slot, k1).releaseTenths};
    const kbd::model::TriggerSettings s2{effectiveFor(state, slot, k2).actuationTenths, effectiveFor(state, slot, k2).rapidTrigger,
                                         effectiveFor(state, slot, k2).pressTenths, effectiveFor(state, slot, k2).releaseTenths};
    const auto t1 = kbd::model::travelFromTenths(depth[0]);
    const auto t2 = kbd::model::travelFromTenths(depth[1]);
    const bool down1 = m_benchKeys[0].step(t1, s1);
    const bool down2 = m_benchKeys[1].step(t2, s2);
    const kbd::model::SocdOutput out = m_socdModel.step(pair.type, {down1, t1}, {down2, t2});
    const double now = ImGui::GetTime();
    m_bench.push_back({now, down1, down2, out});
    while (!m_bench.empty() && m_bench.front().time < now - BENCH_WINDOW) m_bench.pop_front();

    // Left: the two keys as vertical depth gauges (draggable when simulating).
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float gaugeH = em * 9.0f;
    const float gaugeW = em * 3.2f;
    const auto gauge = [&](const int index, const std::string& name, const Color& color, const kbd::model::TriggerSettings& settings,
                           const bool down, const bool sent) {
        ImGui::BeginGroup();
        ImGui::PushID(index);
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max = min + ImVec2(gaugeW, gaugeH);
        ImGui::InvisibleButton("##gauge", ImVec2(gaugeW, gaugeH));
        if (m_simulate && ImGui::IsItemActive()) {
            const float t = std::clamp((ImGui::GetIO().MousePos.y - min.y) / gaugeH, 0.0f, 1.0f);
            m_simTenths[static_cast<std::size_t>(index)] = t * 40.0f;
        }
        if (m_simulate && ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        const float d = std::clamp(depth[static_cast<std::size_t>(index)] / 40.0f, 0.0f, 1.0f);
        draw->AddRectFilled(min, max, toImU32(p.surfaceRaised), em * 0.5f);
        draw->AddRectFilled(min, ImVec2(max.x, min.y + gaugeH * d), toImU32((down ? color : p.textMuted).withAlpha(down ? 170 : 90)), em * 0.5f,
                            ImDrawFlags_RoundCornersTop);
        const float actY = min.y + gaugeH * settings.actuationTenths / 40.0f;
        draw->AddLine(ImVec2(min.x - em * 0.2f, actY), ImVec2(max.x + em * 0.2f, actY), toImU32(p.accentText), 2.0f);
        draw->AddRect(min, max, toImU32(p.border), em * 0.5f);
        // Keycap label at the top, depth at the bottom.
        const ImVec2 nameSize = ImGui::CalcTextSize(name.c_str());
        draw->AddText(ImVec2(min.x + (gaugeW - nameSize.x) * 0.5f, min.y + em * 0.3f), toImU32(p.text), name.c_str());
        char text[16];
        std::snprintf(text, sizeof(text), "%.1f", static_cast<double>(depth[static_cast<std::size_t>(index)]) / 10.0);
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        draw->AddText(ImVec2(min.x + (gaugeW - textSize.x) * 0.5f, max.y - em * 1.3f), toImU32(p.textMuted), text);
        // "Sent" lamp under the gauge.
        const ImVec2 lamp(min.x + gaugeW * 0.5f, max.y + em * 0.9f);
        draw->AddCircleFilled(lamp, em * 0.45f, toImU32(sent ? color : p.surfaceRaised));
        draw->AddCircle(lamp, em * 0.45f, toImU32(sent ? color.lighten(0.3f) : p.border), 0, 1.5f);
        ImGui::Dummy(ImVec2(gaugeW, em * 1.6f));
        ImGui::PopID();
        ImGui::EndGroup();
    };
    gauge(0, name1, p.accentText, s1, down1, out.key1);
    ImGui::SameLine(0.0f, em * 0.8f);
    gauge(1, name2, KEY2_COLOR, s2, down2, out.key2);
    ImGui::SameLine(0.0f, em * 1.5f);

    // Right: what is sent, and a timeline of the last seconds.
    ImGui::BeginGroup();
    const float laneWidth = std::max(em * 10.0f, width - (gaugeW * 2.0f + em * 2.3f));
    std::string sends;
    if (out.key1 && out.key2) sends = name1 + " and " + name2;
    else if (out.key1) sends = name1;
    else if (out.key2) sends = name2;
    else sends = (down1 || down2) ? "nothing (cancelled)" : "nothing";
    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.textMuted));
    ImGui::TextUnformatted("The keyboard sends");
    ImGui::PopStyleColor();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.6f);
    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(out.key1 || out.key2 ? p.text : p.textMuted));
    ImGui::TextUnformatted(sends.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Spacing();

    const float laneH = em * 1.5f;
    const auto lane = [&](const char* label, const bool first, const Color& color) {
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const float labelW = em * 3.0f;
        draw->AddText(ImVec2(min.x, min.y + (laneH - ImGui::GetTextLineHeight()) * 0.5f), toImU32(p.textMuted), label);
        const ImVec2 trackMin(min.x + labelW, min.y);
        const ImVec2 trackMax(min.x + laneWidth, min.y + laneH);
        draw->AddRectFilled(trackMin, trackMax, toImU32(p.surfaceRaised.withAlpha(140)), em * 0.3f);
        const auto xAt = [&](const double t) {
            return trackMin.x + static_cast<float>((t - (now - BENCH_WINDOW)) / BENCH_WINDOW) * (trackMax.x - trackMin.x);
        };
        for (std::size_t i = 1; i < m_bench.size(); ++i) {
            const BenchSample& a = m_bench[i - 1];
            const BenchSample& b = m_bench[i];
            const bool held = first ? a.down1 : a.down2;
            const bool sent = first ? a.out.key1 : a.out.key2;
            if (held) {
                draw->AddRectFilled(ImVec2(xAt(a.time), trackMin.y + laneH * 0.32f), ImVec2(xAt(b.time) + 0.5f, trackMax.y - laneH * 0.32f),
                                    toImU32(color.withAlpha(70)));
            }
            if (sent) {
                draw->AddRectFilled(ImVec2(xAt(a.time), trackMin.y + laneH * 0.12f), ImVec2(xAt(b.time) + 0.5f, trackMax.y - laneH * 0.12f),
                                    toImU32(color));
            }
        }
        ImGui::Dummy(ImVec2(laneWidth, laneH));
    };
    lane(name1.c_str(), true, p.accentText);
    lane(name2.c_str(), false, KEY2_COLOR);
    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.textMuted));
    ImGui::TextWrapped("Last %.0f s. Faint: key held down. Solid: key sent to the computer.%s", BENCH_WINDOW,
                       m_simulate ? " Drag the keys on the left to press them." : " Press the real keys.");
    ImGui::PopStyleColor();
    ImGui::EndGroup();
}

// -----------------------------------------------------------------------------
// Sprint on deep press
// -----------------------------------------------------------------------------
void MovementPanel::drawSprint(const std::size_t slot, const bool canAct, const LayoutKind layout, const SlotDetails& details,
                               const ThemePalette& p) {
    if (!widgets::beginCard("##sprint", p)) {
        widgets::endCard();
        return;
    }
    const float em = ImGui::GetFontSize();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.12f);
    ImGui::TextUnformatted("Sprint on deep press");
    ImGui::PopFont();
    widgets::mutedWrapped("Press a little to walk, all the way down to also hold a second key (Shift runs in most games). Stored "
                          "in the slot as a dynamic keystroke, so it works in every game without this app running.", p);
    ImGui::Spacing();

    // Which keys, and their walk keycode.
    std::vector<std::pair<KeyPosition, std::uint16_t>> targets;
    std::string names;
    for (const KeyPosition& key : m_selection) {
        const auto code = mapping::defaultKeycode(layout, key);
        if (!code) continue;
        targets.emplace_back(key, *code);
        if (!names.empty()) names += ", ";
        names += widgets::keyName(key, layout);
    }

    // Two columns (settings | diagram) in a table, so right-aligned values stay inside their own column.
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(em * 0.6f, 0.0f));
    const bool table = ImGui::BeginTable("##sprint_cols", 2, ImGuiTableFlags_SizingStretchProp);
    ImGui::PopStyleVar();
    if (!table) {
        widgets::endCard();
        return;
    }
    ImGui::TableSetupColumn("settings", ImGuiTableColumnFlags_WidthStretch, 0.46f);
    ImGui::TableSetupColumn("diagram", ImGuiTableColumnFlags_WidthStretch, 0.54f);
    ImGui::TableNextColumn();
    const float left = ImGui::GetContentRegionAvail().x - em * 0.8f;
    ImGui::PushItemWidth(left);
    widgets::labelWithValue("Keys", names.empty() ? std::string("select keys, e.g. W") : names, p);
    const auto sprintKeys = mapping::sprintKeys();
    m_extraKey = std::min(m_extraKey, sprintKeys.size() - 1);
    widgets::sectionLabel("Also hold at the bottom", p);
    ImGui::SetNextItemWidth(left);
    if (ImGui::BeginCombo("##extra", sprintKeys[m_extraKey].name)) {
        for (std::size_t i = 0; i < sprintKeys.size(); ++i) {
            if (ImGui::Selectable(sprintKeys[i].name, i == m_extraKey)) m_extraKey = i;
        }
        ImGui::EndCombo();
    }
    widgets::labelWithValue("Walk from", millimetres(m_shallowTenths), p);
    if (widgets::millimetreSlider("##walk", m_shallowTenths, 1, 35, left, p)) {
        m_deepTenths = std::max<std::uint8_t>(m_deepTenths, static_cast<std::uint8_t>(m_shallowTenths + 3));
    }
    widgets::labelWithValue("Run from", millimetres(m_deepTenths), p);
    if (widgets::millimetreSlider("##run", m_deepTenths, 4, 40, left, p)) {
        m_shallowTenths = std::min<std::uint8_t>(m_shallowTenths, static_cast<std::uint8_t>(m_deepTenths - 3));
    }
    ImGui::PopItemWidth();

    ImGui::TableNextColumn();
    std::optional<float> live;
    if (!targets.empty()) live = liveTenths(targets.front().first);
    ImGui::Dummy(ImVec2(0, em * 0.4f));
    sprintDiagram(ImGui::GetContentRegionAvail().x, m_shallowTenths, m_deepTenths, live, p);
    widgets::mutedWrapped(live ? "Green: how far the first selected key is pressed right now." :
                                 "The second key is held only while the key is below the run point.", p);

    // Apply / remove for the selection.
    const bool anyDynamic = std::any_of(targets.begin(), targets.end(), [&](const auto& t) {
        return details.at(t.first).mode == kbd::AdvancedMode::DKS;
    });
    ImGui::BeginDisabled(!canAct || targets.empty());
    const std::string apply = targets.size() == 1 ? "Apply and save to " + names : "Apply and save to " + std::to_string(targets.size()) + " keys";
    if (widgets::primaryButton(targets.empty() ? "Apply and save" : apply.c_str(), ImVec2(0, 0), p)) {
        SlotDetails planned = details; // hands out a different OKMC entry to every key
        std::vector<AdvancedKeyChange> changes;
        for (const auto& [key, code] : targets) {
            const auto index = planned.okmcIndexFor(key);
            if (!index) break; // all 20 entries in use
            AdvancedKeyChange change;
            change.key   = key;
            change.kind  = AdvancedKeyChange::Kind::DynamicKeystroke;
            change.index = *index;
            change.okmc  = kbd::deepPressCombo(code, sprintKeys[m_extraKey].code, m_shallowTenths, m_deepTenths);
            planned.set(key, {kbd::AdvancedMode::DKS, *index});
            changes.push_back(change);
        }
        m_commands.applyAdvancedKeys(static_cast<std::uint8_t>(slot), std::move(changes), true);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!canAct || !anyDynamic);
    if (widgets::secondaryButton("Remove", ImVec2(0, 0), p)) {
        std::vector<AdvancedKeyChange> changes;
        for (const auto& [key, code] : targets) {
            if (details.at(key).mode == kbd::AdvancedMode::DKS) changes.push_back(AdvancedKeyChange{key, AdvancedKeyChange::Kind::Clear});
        }
        m_commands.applyAdvancedKeys(static_cast<std::uint8_t>(slot), std::move(changes), true);
    }
    ImGui::EndDisabled();

    ImGui::EndTable();

    // Keys of this slot that already have a dynamic keystroke.
    bool header = false;
    for (std::uint8_t row = 0; row < MATRIX_ROWS; ++row) {
        for (std::uint8_t col = 0; col < MATRIX_COLS; ++col) {
            const KeyPosition key{row, col};
            const KeyAdvanced advanced = details.at(key);
            if (advanced.mode != kbd::AdvancedMode::DKS || !isKeyOnBoard(key) || advanced.data >= details.okmc.size()) continue;
            if (!header) {
                ImGui::Spacing();
                ImGui::Separator();
                widgets::sectionLabel("In this slot", p);
                header = true;
            }
            ImGui::PushID(row * 16 + col);
            const kbd::OkmcConfig& okmc = details.okmc[advanced.data];
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", widgets::keyName(key, layout).c_str());
            ImGui::SameLine(em * 5.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.textMuted));
            ImGui::TextUnformatted(describeCombo(okmc).c_str());
            ImGui::PopStyleColor();
            if (mapping::isDeepPressCombo(okmc)) {
                ImGui::SameLine();
                widgets::alignRight(widgets::buttonWidth("Edit"));
                if (widgets::secondaryButton("Edit", ImVec2(0, 0), p)) {
                    m_selection     = {key};
                    m_shallowTenths = okmc.shallowActTenths;
                    m_deepTenths    = okmc.deepActTenths;
                    const auto it = std::find_if(sprintKeys.begin(), sprintKeys.end(), [&](const auto& o) { return o.code == okmc.keycodes[1]; });
                    if (it != sprintKeys.end()) m_extraKey = static_cast<std::size_t>(it - sprintKeys.begin());
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::Spacing();
    widgets::mutedWrapped("Want walking speed to follow how far you press instead? Make the slot a gamepad profile with the "
                          "\"Analog movement\" preset (Gamepad page).", p);
    widgets::endCard();
}

} // namespace ui
