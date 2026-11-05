#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "ui/imgui/GamepadPanel.h"
#include "ui/imgui/Glyphs.h"
#include "ui/ControllerMapping.h"
#include "ui/imgui/ImGuiTheme.h"
#include "ui/imgui/Plots.h"
#include "ui/imgui/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>

namespace ui {

namespace {

    using gamepad::Axis;
    using gamepad::Button;
    using gamepad::LabelStyle;
    using mapping::TargetGroup;
    namespace GT = kbd::GamepadTarget;

    constexpr std::size_t MAX_PREVIEW_KEYS = 16; // one realtime-travel request per key per poll

    Color groupColor(const TargetGroup group, const ThemePalette& p) {
        switch (group) {
            case TargetGroup::LeftStick:   return p.accentText;
            case TargetGroup::RightStick:  return Color{0x38, 0xBD, 0xF8};
            case TargetGroup::Triggers:    return p.warning;
            case TargetGroup::FaceButtons: return p.success;
            case TargetGroup::Shoulders:   return Color{0xF4, 0x72, 0xB6};
            case TargetGroup::Dpad:        return Color{0x2D, 0xD4, 0xBF};
            case TargetGroup::Menu:        return p.textMuted.lighten(0.15f);
            case TargetGroup::StickClicks: return p.accentHover.lighten(0.25f);
            case TargetGroup::HidOnly:     break;
        }
        return p.textMuted;
    }

    std::string percent(const float value) {
        char text[16];
        std::snprintf(text, sizeof(text), "%.0f%%", static_cast<double>(value) * 100.0);
        return text;
    }

    std::string signedValue(const float value) {
        char text[16];
        std::snprintf(text, sizeof(text), "%+.2f", static_cast<double>(value));
        return text;
    }

    // A rounded text chip; returns its width.
    float chip(ImDrawList* draw, const ImVec2 at, const char* text, const Color& fill, const Color& ink, const float height) {
        const ImVec2 size = ImGui::CalcTextSize(text);
        const float pad = height * 0.32f;
        const ImVec2 max(at.x + size.x + pad * 2.0f, at.y + height);
        draw->AddRectFilled(at, max, toImU32(fill), height * 0.3f);
        draw->AddText(ImVec2(at.x + pad, at.y + (height - size.y) * 0.5f), toImU32(ink), text);
        return max.x - at.x;
    }

    constexpr gamepad::Attack ATTACKS[] = {gamepad::Attack::West, gamepad::Attack::North, gamepad::Attack::RightShoulder,
                                           gamepad::Attack::South, gamepad::Attack::East, gamepad::Attack::RightTrigger,
                                           gamepad::Attack::LeftShoulder, gamepad::Attack::LeftTrigger};

    std::optional<Button> faceButtonOf(const gamepad::Attack attack) {
        switch (attack) {
            case gamepad::Attack::South: return Button::South;
            case gamepad::Attack::East:  return Button::East;
            case gamepad::Attack::West:  return Button::West;
            case gamepad::Attack::North: return Button::North;
            default:                     return std::nullopt;
        }
    }

    // What XInputGetState would return, in the model's -1..1 / 0..1 units.
    gamepad::PadState padFromReport(const kbd::model::XInputReport& r) {
        namespace XB = kbd::model::XButton;
        const auto stick = [](const std::int16_t v) { return v < 0 ? static_cast<float>(v) / 32768.0f : static_cast<float>(v) / 32767.0f; };
        gamepad::PadState pad;
        pad.set(Axis::LeftX, stick(r.leftX));
        pad.set(Axis::LeftY, stick(r.leftY));
        pad.set(Axis::RightX, stick(r.rightX));
        pad.set(Axis::RightY, stick(r.rightY));
        pad.set(Axis::LeftTrigger, static_cast<float>(r.leftTrigger) / 255.0f);
        pad.set(Axis::RightTrigger, static_cast<float>(r.rightTrigger) / 255.0f);
        const std::pair<std::uint16_t, Button> bits[] = {
            {XB::A, Button::South}, {XB::B, Button::East}, {XB::X, Button::West}, {XB::Y, Button::North},
            {XB::LB, Button::LeftShoulder}, {XB::RB, Button::RightShoulder}, {XB::BACK, Button::Back}, {XB::START, Button::Start},
            {XB::LEFT_THUMB, Button::LeftStick}, {XB::RIGHT_THUMB, Button::RightStick}, {XB::DPAD_UP, Button::DpadUp},
            {XB::DPAD_DOWN, Button::DpadDown}, {XB::DPAD_LEFT, Button::DpadLeft}, {XB::DPAD_RIGHT, Button::DpadRight},
            {XB::GUIDE, Button::Guide}};
        for (const auto& [bit, button] : bits) pad.set(button, (r.buttons & bit) != 0);
        return pad;
    }

    // The controller part a target lights up, and for stick directions which way (numpad: 8 up, 2 down, 4, 6).
    std::optional<std::pair<art::Part, int>> partOfTarget(const std::uint8_t target) {
        switch (target) {
            case GT::LEFT_STICK_LEFT:   return std::pair{art::Part::LeftStick, 4};
            case GT::LEFT_STICK_RIGHT:  return std::pair{art::Part::LeftStick, 6};
            case GT::LEFT_STICK_DOWN:   return std::pair{art::Part::LeftStick, 2};
            case GT::LEFT_STICK_UP:     return std::pair{art::Part::LeftStick, 8};
            case GT::LEFT_TRIGGER:      return std::pair{art::Part::LT, 5};
            case GT::RIGHT_TRIGGER:     return std::pair{art::Part::RT, 5};
            case GT::RIGHT_STICK_LEFT:  return std::pair{art::Part::RightStick, 4};
            case GT::RIGHT_STICK_RIGHT: return std::pair{art::Part::RightStick, 6};
            case GT::RIGHT_STICK_DOWN:  return std::pair{art::Part::RightStick, 2};
            case GT::RIGHT_STICK_UP:    return std::pair{art::Part::RightStick, 8};
            default: break;
        }
        if (!GT::isButton(target)) return std::nullopt;
        static constexpr art::Part BUTTONS[] = {art::Part::A, art::Part::B, art::Part::X, art::Part::Y, art::Part::LB, art::Part::RB,
                                                art::Part::View, art::Part::Menu, art::Part::LeftStick, art::Part::RightStick,
                                                art::Part::DpadUp, art::Part::DpadDown, art::Part::DpadLeft, art::Part::DpadRight,
                                                art::Part::Guide};
        const std::size_t n = target - GT::BUTTON_BASE;
        if (n >= std::size(BUTTONS)) return std::nullopt;
        return std::pair{BUTTONS[n], 5};
    }

    // Where a label for a part goes, as a direction away from the part: 8 up, 2 down, 4 left, 6 right,
    // 5 on the part itself (stick clicks sit on the cap), 0 outward from the face-button cluster.
    int labelDirection(const art::Part part, const int stickDirection) {
        switch (part) {
            case art::Part::LeftStick:
            case art::Part::RightStick: return stickDirection;
            case art::Part::DpadUp: return 8;
            case art::Part::DpadDown: return 2;
            case art::Part::DpadLeft: return 4;
            case art::Part::DpadRight: return 6;
            case art::Part::A: case art::Part::B: case art::Part::X: case art::Part::Y: return 0;
            default: return 8;
        }
    }

    // The controller target of a face button (A B X Y in XInput order).
    std::optional<std::uint8_t> faceTarget(const gamepad::Button button) {
        switch (button) {
            case gamepad::Button::South: return GT::button(0);
            case gamepad::Button::East: return GT::button(1);
            case gamepad::Button::West: return GT::button(2);
            case gamepad::Button::North: return GT::button(3);
            default: return std::nullopt;
        }
    }

    struct PartLabel {
        std::string text;
        ImVec2      anchor;  // edge of the part the label belongs to
        ImVec2      centre;  // current chip centre
        ImVec2      size;    // chip size
    };

    // Pushes overlapping chips apart (a few rounds of pairwise separation along the axis that needs the least
    // movement), keeping them inside `bounds`.
    void separateLabels(std::vector<PartLabel>& labels, const ImVec2 boundsMin, const ImVec2 boundsMax, const float gap) {
        for (int round = 0; round < 48; ++round) {
            bool moved = false;
            for (std::size_t i = 0; i < labels.size(); ++i) {
                for (std::size_t j = i + 1; j < labels.size(); ++j) {
                    PartLabel& a = labels[i];
                    PartLabel& b = labels[j];
                    const float overlapX = (a.size.x + b.size.x) * 0.5f + gap - std::fabs(a.centre.x - b.centre.x);
                    const float overlapY = (a.size.y + b.size.y) * 0.5f + gap - std::fabs(a.centre.y - b.centre.y);
                    if (overlapX <= 0.0f || overlapY <= 0.0f) continue;
                    moved = true;
                    if (overlapY < overlapX) {
                        const float push = overlapY * 0.5f + 0.5f;
                        const float sign = a.centre.y <= b.centre.y ? -1.0f : 1.0f;
                        a.centre.y += sign * push;
                        b.centre.y -= sign * push;
                    } else {
                        const float push = overlapX * 0.5f + 0.5f;
                        const float sign = a.centre.x <= b.centre.x ? -1.0f : 1.0f;
                        a.centre.x += sign * push;
                        b.centre.x -= sign * push;
                    }
                }
            }
            for (PartLabel& l : labels) {
                l.centre.x = std::clamp(l.centre.x, boundsMin.x + l.size.x * 0.5f, boundsMax.x - l.size.x * 0.5f);
                l.centre.y = std::clamp(l.centre.y, boundsMin.y + l.size.y * 0.5f, boundsMax.y - l.size.y * 0.5f);
            }
            if (!moved) break;
        }
    }

    struct CurvePreset {
        const char*        name;
        const char*        tip;
        kbd::ResponseCurve curve;
    };
    const CurvePreset CURVE_PRESETS[] = {
        {"Linear", "The firmware default: stick travel follows key travel.", kbd::DEFAULT_CURVE},
        {"Precise", "Slow start for fine aiming and walking, full speed near the bottom.", {{{2, 0}, {16, 20}, {32, 70}, {38, 127}}}},
        {"Responsive", "Strong response early, for quick turns.", {{{2, 0}, {8, 60}, {20, 110}, {32, 127}}}},
        {"Instant", "Almost digital: full deflection after a light press.", {{{1, 0}, {4, 127}, {4, 127}, {4, 127}}}},
    };

} // namespace

GamepadPanel::GamepadPanel(IProfileCommands& commands, std::shared_ptr<gamepad::IGamepadSource> source, std::shared_ptr<Workspace> workspace)
    : m_commands(commands), m_source(std::move(source)), m_workspace(std::move(workspace)) {}

const char* GamepadPanel::title() const noexcept {
    return "Gamepad";
}

const char* GamepadPanel::subtitle() const noexcept {
    return "Turn keys into an Xbox controller: analog sticks, triggers and buttons for games and emulators";
}

widgets::Icon GamepadPanel::icon() const noexcept {
    return widgets::Icon::Gamepad;
}

void GamepadPanel::onShown() {
    m_controllerRead = false; // read the mode and the curve again: another tool may have changed them
}

void GamepadPanel::onHidden() {
    watch({});
}

void GamepadPanel::watch(std::vector<KeyPosition> keys) {
    if (keys.size() > MAX_PREVIEW_KEYS) keys.resize(MAX_PREVIEW_KEYS);
    if (keys == m_watching) return;
    m_watching = keys;
    m_commands.watchLiveTravel(std::move(keys));
}

std::optional<std::uint8_t> GamepadPanel::targetOf(const SlotDetails& details, const KeyPosition& key) const {
    if (const auto it = m_draft.find(key); it != m_draft.end()) return it->second;
    const KeyAdvanced advanced = details.at(key);
    if (advanced.mode == kbd::AdvancedMode::GAMEPAD) return advanced.data;
    return std::nullopt;
}

std::vector<std::pair<KeyPosition, std::uint8_t>> GamepadPanel::gamepadKeys(const SlotDetails& details) const {
    std::vector<std::pair<KeyPosition, std::uint8_t>> keys;
    for (std::uint8_t row = 0; row < MATRIX_ROWS; ++row) {
        for (std::uint8_t col = 0; col < MATRIX_COLS; ++col) {
            const KeyPosition key{row, col};
            if (!isKeyOnBoard(key)) continue;
            if (const auto target = targetOf(details, key)) keys.emplace_back(key, *target);
        }
    }
    return keys;
}

art::Style GamepadPanel::artStyle(const ThemePalette& p) const {
    art::Style style;
    style.body      = toImU32(p.surfaceRaised.mix(p.surface, 0.55f));
    style.outline   = toImU32(p.text.mix(p.textMuted, 0.25f));
    style.pressed   = toImU32(p.accent);
    style.stickCap  = toImU32(p.surfaceRaised.lighten(0.08f));
    style.highlight = toImU32(p.warning);
    return style;
}

void GamepadPanel::pushState(const gamepad::PadState& pad, const std::uint64_t nowNs) {
    if (pad != m_pad || m_history.entries().empty()) {
        gamepad::Sample sample{nowNs, pad};
        m_history.push(sample);
        m_leftTrail.push(pad.axis(Axis::LeftX), pad.axis(Axis::LeftY), nowNs);
        m_rightTrail.push(pad.axis(Axis::RightX), pad.axis(Axis::RightY), nowNs);
        m_leftTest.add(pad.axis(Axis::LeftX), pad.axis(Axis::LeftY));
        m_rightTest.add(pad.axis(Axis::RightX), pad.axis(Axis::RightY));
    }
    m_pad = pad;
}

void GamepadPanel::consumeSdl(const Frame& f) {
    (void)f;
    if (!m_source) return;
    for (const gamepad::Sample& sample : m_source->takeSamples()) {
        m_history.push(sample);
        m_leftTrail.push(sample.state.axis(Axis::LeftX), sample.state.axis(Axis::LeftY), sample.timestampNs);
        m_rightTrail.push(sample.state.axis(Axis::RightX), sample.state.axis(Axis::RightY), sample.timestampNs);
        m_leftTest.add(sample.state.axis(Axis::LeftX), sample.state.axis(Axis::LeftY));
        m_rightTest.add(sample.state.axis(Axis::RightX), sample.state.axis(Axis::RightY));
    }
    m_pad = m_source->current();
}

void GamepadPanel::runPreview(const Frame& f) {
    std::vector<kbd::model::GamepadKey> keys;
    for (const auto& [key, target] : gamepadKeys(f.details)) {
        kbd::model::GamepadKey input{target, 0, false};
        for (const LiveKeyTravel& reading : m_live) {
            if (reading.key == key) {
                input.travel  = reading.travelRaw;
                input.pressed = reading.pressed;
            }
        }
        keys.push_back(input);
    }
    const kbd::ResponseCurve curve = m_view == View::Curve ? m_curveDraft : f.state->controller.curve.value_or(kbd::DEFAULT_CURVE);
    const kbd::model::XInputReport report = m_previewModel.step(keys, curve);
    pushState(padFromReport(report), static_cast<std::uint64_t>(ImGui::GetTime() * 1e9));
}

void GamepadPanel::render(const AppSnapshot& state, const Theme& theme) {
    const ThemePalette& p = theme.palette;
    plots::applyStyle(p);

    Frame f;
    f.state   = &state;
    f.slot    = m_workspace->slot(state);
    f.layout  = m_workspace->keyboardLayout(state);
    f.canAct  = state.device.connected && !state.busy;
    f.details = state.details[f.slot].value_or(SlotDetails{});
    f.labels  = m_workspace->labelStyle();
    if (f.slot != m_draftSlot) {
        m_draft.clear();
        m_draftSlot = f.slot;
    }
    // Drop edits the keyboard now agrees with (after Apply + read-back).
    for (auto it = m_draft.begin(); it != m_draft.end();) {
        const KeyAdvanced stored = f.details.at(it->first);
        const bool same = it->second ? (stored.mode == kbd::AdvancedMode::GAMEPAD && stored.data == *it->second)
                                     : stored.mode != kbd::AdvancedMode::GAMEPAD;
        it = same && state.details[f.slot] ? m_draft.erase(it) : std::next(it);
    }
    const auto padKeys = gamepadKeys(f.details);
    f.padKeys = padKeys.size();

    if (!m_controllerRead && state.device.connected && !state.busy) {
        m_controllerRead = true;
        m_commands.readControllerSettings();
    }

    // Which controller to show: what Windows receives (the keyboard's pad through SDL), or the preview.
    if (m_source) {
        for (const gamepad::PadInfo& pad : m_source->pads()) {
            if (pad.isKeychron()) f.keyboardPad = true;
        }
    }
    const bool otherPad = m_source && !m_source->pads().empty();
    switch (m_sourceMode) {
        case Source::Auto:    f.preview = !f.keyboardPad && (f.padKeys > 0 || !otherPad); break;
        case Source::Windows: f.preview = false; break;
        case Source::Preview: f.preview = true; break;
    }
    // In the mapping and curve views the preview shows unapplied edits, so prefer it there.
    if (m_view != View::Controller && f.padKeys > 0 && m_sourceMode == Source::Auto) f.preview = true;

    // Live travel: every gamepad key for the preview, else the keys the page looks at.
    std::vector<KeyPosition> wanted;
    if (f.preview && state.device.connected) {
        for (const auto& [key, target] : padKeys) wanted.push_back(key);
    }
    if (m_view == View::Mapping) {
        for (const KeyPosition& key : m_selection) {
            if (std::find(wanted.begin(), wanted.end(), key) == wanted.end()) wanted.insert(wanted.begin(), key);
        }
    }
    if (m_view == View::Curve && m_curveKey && std::find(wanted.begin(), wanted.end(), *m_curveKey) == wanted.end()) {
        wanted.insert(wanted.begin(), *m_curveKey);
    }
    watch(wanted);
    m_live = m_commands.liveTravel();

    if (f.preview != m_lastWasPreview) {
        m_previewModel.reset();
        m_history.clear();
        m_leftTrail.clear();
        m_rightTrail.clear();
        m_lastWasPreview = f.preview;
    }
    if (f.preview) {
        if (m_source) (void)m_source->takeSamples(); // keep SDL's queue short
        runPreview(f);
    } else {
        consumeSdl(f);
    }
    const std::uint64_t now = f.preview ? static_cast<std::uint64_t>(ImGui::GetTime() * 1e9) : (m_source ? m_source->nowNs() : 0);
    m_leftTrail.expire(now);
    m_rightTrail.expire(now);

    drawModeCard(f, p);

    static constexpr const char* VIEWS[] = {"Controller", "Key mapping", "Response curve"};
    const int clicked = widgets::segmented("##views", VIEWS, static_cast<int>(m_view), p, ImGui::GetFontSize() * 26.0f);
    if (clicked >= 0) m_view = static_cast<View>(clicked);
    ImGui::Spacing();
    switch (m_view) {
        case View::Controller: drawController(f, p); break;
        case View::Mapping:    drawMapping(f, p); break;
        case View::Curve:      drawCurve(f, p); break;
    }
}

// -----------------------------------------------------------------------------
// Gamepad profile on / off, controller mode, replug
// -----------------------------------------------------------------------------
void GamepadPanel::drawModeCard(const Frame& f, const ThemePalette& p) {
    const AppSnapshot& state = *f.state;
    const float em = ImGui::GetFontSize();

    // Replug walk-through: advance on what the keyboard does.
    switch (m_replug) {
        case Replug::Unplug:
            if (!state.device.connected) {
                m_replug = Replug::PlugIn;
                m_replugSince = ImGui::GetTime();
            }
            break;
        case Replug::PlugIn:
            if (state.device.connected) {
                m_replug = Replug::Verify;
                m_replugSince = ImGui::GetTime();
                m_commands.readControllerSettings();
            }
            break;
        case Replug::Verify:
            if (f.keyboardPad || ImGui::GetTime() - m_replugSince > 8.0) {
                if (f.keyboardPad) m_replug = Replug::None;
            }
            break;
        case Replug::None: break;
    }
    if (m_replug != Replug::None) {
        drawReplug(f, p);
        ImGui::Spacing();
        return;
    }

    if (!state.details[f.slot]) {
        if (!widgets::beginCard("##gp_mode", p)) {
            widgets::endCard();
            return;
        }
        widgets::mutedWrapped(state.device.connected ? "Reading the slot from the keyboard..."
                                                     : "Connect the keyboard (USB-C) to set up gamepad keys. The preview and the "
                                                       "mapping still work with the profile file.", p);
        widgets::endCard();
        ImGui::Spacing();
        if (state.device.connected) return;
    }

    if (f.padKeys == 0 && m_draft.empty()) {
        drawSetup(f, p);
        ImGui::Spacing();
        return;
    }

    // A gamepad profile: one status line with the controller mode.
    if (!widgets::beginCard("##gp_mode", p)) {
        widgets::endCard();
        return;
    }
    const std::optional<std::uint8_t> mode = state.controller.mode;
    const bool xinput = mode && (*mode & kbd::ControllerMode::XINPUT) != 0;
    const bool typing = mode && (*mode & kbd::ControllerMode::TYPING) != 0;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    widgets::drawIcon(ImGui::GetWindowDrawList(), widgets::Icon::Gamepad, ImVec2(at.x + em * 0.8f, at.y + ImGui::GetFrameHeight() * 0.5f),
                      em * 1.3f, toImU32(p.accentText), std::max(1.4f, em * 0.1f));
    ImGui::Dummy(ImVec2(em * 1.8f, ImGui::GetFrameHeight()));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Gamepad profile  -  %zu %s", f.padKeys, f.padKeys == 1 ? "key drives the controller" : "keys drive the controller");
    ImGui::SameLine(0.0f, em);
    if (!mode) {
        widgets::pill("controller mode not read", p.surfaceRaised, p.textMuted);
    } else if (xinput) {
        widgets::pill(typing ? "XInput, keys also type" : "XInput", p.surfaceRaised, p.success, p.success);
    } else {
        widgets::pill("HID joystick mode", p.surfaceRaised, p.warning, p.warning);
    }

    const char* offLabel = "Turn off";
    widgets::alignRight(widgets::buttonWidth(offLabel) + (mode && !xinput ? widgets::buttonWidth("Switch to XInput") + ImGui::GetStyle().ItemSpacing.x : 0.0f));
    if (mode && !xinput) {
        ImGui::BeginDisabled(!f.canAct);
        if (widgets::primaryButton("Switch to XInput", ImVec2(0, 0), p)) {
            m_commands.applyControllerMode(static_cast<std::uint8_t>(*mode | kbd::ControllerMode::XINPUT));
            m_replug = Replug::Unplug;
            m_replugSince = ImGui::GetTime();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Emulators and most games read XInput (Xbox) controllers.");
        ImGui::SameLine();
    }
    ImGui::BeginDisabled(!f.canAct || f.padKeys == 0);
    if (widgets::secondaryButton(offLabel, ImVec2(0, 0), p)) m_confirmOff = true;
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Make every gamepad key of this slot a normal key again");

    if (m_confirmOff) {
        ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.warning));
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Turn %zu gamepad keys of %s back into normal keys?", f.padKeys, slotLabel(f.slot).c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (widgets::primaryButton("Yes, turn off", ImVec2(0, 0), p)) {
            std::vector<AdvancedKeyChange> changes;
            for (std::uint8_t row = 0; row < MATRIX_ROWS; ++row) {
                for (std::uint8_t col = 0; col < MATRIX_COLS; ++col) {
                    if (f.details.at({row, col}).mode == kbd::AdvancedMode::GAMEPAD) {
                        changes.push_back(AdvancedKeyChange{{row, col}, AdvancedKeyChange::Kind::Clear});
                    }
                }
            }
            m_draft.clear();
            m_commands.applyAdvancedKeys(static_cast<std::uint8_t>(f.slot), std::move(changes), true);
            m_confirmOff = false;
        }
        ImGui::SameLine();
        if (widgets::secondaryButton("Cancel##off", ImVec2(0, 0), p)) m_confirmOff = false;
    }
    {
        bool automatic = m_commands.autoControllerMode();
        if (widgets::toggle("Switch XInput with the profile", automatic, p)) m_commands.setAutoControllerMode(automatic);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("When a slot becomes active (from this app, the tray, a hotkey or Fn + P), XInput is turned on\n"
                              "if the slot has gamepad keys and off if it has none. The keyboard switches after a replug.");
        }
    }
    if (mode && !xinput) {
        widgets::mutedWrapped("The keyboard is in generic HID joystick mode: DirectInput tools see it, most games and emulators "
                              "do not. Switch to XInput unless you need the extra buttons.", p);
    }
    widgets::endCard();
    ImGui::Spacing();
}

void GamepadPanel::drawSetup(const Frame& f, const ThemePalette& p) {
    const AppSnapshot& state = *f.state;
    if (!widgets::beginCard("##gp_setup", p)) {
        widgets::endCard();
        return;
    }
    const float em = ImGui::GetFontSize();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.12f);
    ImGui::Text("%s is a keyboard profile", slotLabel(f.slot).c_str());
    ImGui::PopFont();
    widgets::mutedWrapped("Make it a gamepad profile and the chosen keys act as an Xbox controller: sticks follow how far you "
                          "press, triggers too, buttons press at the actuation point. The other slots stay as they are, so you "
                          "can switch between a typing profile and a controller profile with Fn + P or the tray.", p);
    ImGui::Spacing();

    widgets::sectionLabel("Start with", p);
    const auto& presets = mapping::presets();
    m_setupPreset = std::min(m_setupPreset, presets.size() - 1);
    std::vector<const char*> names;
    for (const auto& preset : presets) names.push_back(preset.name);
    const int picked = widgets::segmented("##preset", names, static_cast<int>(m_setupPreset), p, ImGui::GetContentRegionAvail().x);
    if (picked >= 0) m_setupPreset = static_cast<std::size_t>(picked);
    widgets::mutedWrapped(presets[m_setupPreset].description, p);
    ImGui::Spacing();

    const std::optional<std::uint8_t> mode = state.controller.mode;
    if (mode) m_setupTyping = m_setupTyping || (*mode & kbd::ControllerMode::TYPING) != 0;
    widgets::toggle("Gamepad keys also type their letter", m_setupTyping, p);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("On: W still types W (menus, chat) while it moves the stick.\nOff: gamepad keys only drive the controller.\n"
                          "This is a keyboard-wide setting.");
    }
    const std::uint8_t wantedMode = static_cast<std::uint8_t>(kbd::ControllerMode::XINPUT | (m_setupTyping ? kbd::ControllerMode::TYPING : 0));
    const bool modeChange = !mode || *mode != wantedMode;

    ImGui::Spacing();
    ImGui::BeginDisabled(!f.canAct || !state.details[f.slot]);
    if (widgets::primaryButton("Make it a gamepad profile", ImVec2(0, em * 2.1f), p)) {
        std::vector<AdvancedKeyChange> changes;
        for (const auto& [key, target] : presets[m_setupPreset].bindings) {
            AdvancedKeyChange change;
            change.key   = key;
            change.kind  = AdvancedKeyChange::Kind::Gamepad;
            change.index = target;
            changes.push_back(change);
        }
        m_commands.applyAdvancedKeys(static_cast<std::uint8_t>(f.slot), std::move(changes), true);
        if (modeChange) {
            m_commands.applyControllerMode(wantedMode);
            if (!mode || (*mode & kbd::ControllerMode::XINPUT) == 0) {
                m_replug = Replug::Unplug;
                m_replugSince = ImGui::GetTime();
            }
        }
        m_view = View::Mapping;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.textMuted));
    if (!mode || (*mode & kbd::ControllerMode::XINPUT) == 0) {
        ImGui::TextUnformatted("Also turns on XInput mode; you will be asked to replug the cable once.");
    } else {
        ImGui::TextUnformatted("XInput mode is already on: no replug needed.");
    }
    ImGui::PopStyleColor();
    widgets::endCard();
}

void GamepadPanel::drawReplug(const Frame& f, const ThemePalette& p) {
    if (!widgets::beginCard("##gp_replug", p)) {
        widgets::endCard();
        return;
    }
    (void)f;
    const float em = ImGui::GetFontSize();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.12f);
    ImGui::TextUnformatted("One replug to switch the controller mode");
    ImGui::PopFont();
    widgets::mutedWrapped("The keyboard tells Windows what kind of device it is only when it is plugged in, so the new controller "
                          "mode takes effect after a replug. Your slots and settings stay as they are.", p);
    ImGui::Spacing();

    struct Step {
        const char* text;
        Replug      step;
    };
    const Step steps[] = {{"Unplug the USB-C cable", Replug::Unplug}, {"Plug it back in", Replug::PlugIn},
                          {"Windows finds the controller", Replug::Verify}};
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (std::size_t i = 0; i < std::size(steps); ++i) {
        const bool done = static_cast<int>(m_replug) > static_cast<int>(steps[i].step);
        const bool current = m_replug == steps[i].step;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const ImVec2 c(at.x + em * 0.7f, at.y + ImGui::GetFrameHeight() * 0.5f);
        if (done) {
            draw->AddCircleFilled(c, em * 0.55f, toImU32(p.success));
            draw->AddLine(c + ImVec2(-em * 0.25f, 0), c + ImVec2(-em * 0.05f, em * 0.2f), toImU32(p.background), 2.0f);
            draw->AddLine(c + ImVec2(-em * 0.05f, em * 0.2f), c + ImVec2(em * 0.28f, -em * 0.2f), toImU32(p.background), 2.0f);
        } else if (current) {
            ImGui::SetCursorScreenPos(c - ImVec2(em * 0.45f, ImGui::GetTextLineHeight() * 0.5f));
            widgets::spinner(em * 0.45f, p.accentText);
            ImGui::SetCursorScreenPos(at);
        } else {
            draw->AddCircle(c, em * 0.55f, toImU32(p.border), 0, 1.5f);
        }
        ImGui::Dummy(ImVec2(em * 1.6f, ImGui::GetFrameHeight()));
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(current ? p.text : done ? p.textMuted : p.textMuted.withAlpha(150)));
        ImGui::TextUnformatted(steps[i].text);
        ImGui::PopStyleColor();
    }
    if (m_replug == Replug::Verify && ImGui::GetTime() - m_replugSince > 8.0) {
        ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.warning));
        ImGui::TextWrapped("No XInput controller from the keyboard yet. If Windows shows it in \"Set up USB game controllers\", "
                           "everything is fine; otherwise check the mode below and replug once more.");
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    if (widgets::secondaryButton(m_replug == Replug::Verify ? "Done" : "Skip for now", ImVec2(0, 0), p)) m_replug = Replug::None;
    widgets::endCard();
}

// -----------------------------------------------------------------------------
// Controller view
// -----------------------------------------------------------------------------
void GamepadPanel::drawSourceLine(const Frame& f, const ThemePalette& p) {
    const float em = ImGui::GetFontSize();
    static constexpr const char* SOURCES[] = {"Auto", "From Windows", "Key preview"};
    const int picked = widgets::segmented("##source", SOURCES, static_cast<int>(m_sourceMode), p, em * 22.0f);
    if (picked >= 0) m_sourceMode = static_cast<Source>(picked);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("From Windows: the controller as games receive it (needs XInput mode).\n"
                          "Key preview: computed here from the live key travel with the keyboard's own maths,\n"
                          "including mapping and curve edits you have not applied yet.");
    }
    ImGui::SameLine(0.0f, em);
    ImGui::AlignTextToFramePadding();
    if (f.preview) {
        widgets::pill("PREVIEW", p.surfaceRaised, p.accentText, p.accentText);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.textMuted));
        ImGui::TextUnformatted(f.state->device.connected ? "computed from the key travel" : "connect the keyboard to drive it");
        ImGui::PopStyleColor();
        return;
    }
    const std::vector<gamepad::PadInfo> pads = m_source ? m_source->pads() : std::vector<gamepad::PadInfo>{};
    const auto selected = m_source ? m_source->selectedPad() : std::nullopt;
    const gamepad::PadInfo* current = nullptr;
    for (const auto& pad : pads) {
        if (selected && pad.id == *selected) current = &pad;
    }
    if (current == nullptr) {
        widgets::pill("NO CONTROLLER", p.surfaceRaised, p.warning, p.warning);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.textMuted));
        ImGui::TextUnformatted("Windows reports no controller (XInput mode needs a replug after it is turned on)");
        ImGui::PopStyleColor();
        return;
    }
    widgets::pill(current->isKeychron() ? "KEYBOARD" : "CONTROLLER", p.surfaceRaised, p.text, p.success);
    ImGui::SameLine();
    const std::string label = current->name + "  (" + gamepad::familyName(current->family) + ")";
    if (pads.size() > 1) {
        ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x, em * 22.0f));
        if (ImGui::BeginCombo("##pad", label.c_str())) {
            for (const auto& pad : pads) {
                if (ImGui::Selectable((pad.name + "##" + std::to_string(pad.id)).c_str(), current->id == pad.id)) m_source->selectPad(pad.id);
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::TextUnformatted(label.c_str());
    }
}

void GamepadPanel::drawController(const Frame& f, const ThemePalette& p) {
    drawSourceLine(f, p);
    ImGui::Spacing();

    const float em = ImGui::GetFontSize();
    const float avail = ImGui::GetContentRegionAvail().x;
    const float artWidth = std::floor(std::min(avail * 0.62f, em * 38.0f));
    const float artHeight = artWidth / art::aspect(art::modelFor(f.labels));

    ImGui::BeginGroup();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const art::Layout layout = art::draw(art::modelFor(f.labels), draw, origin, artWidth, m_pad, artStyle(p));
    // Stick trails on top of the caps' wells.
    const std::uint64_t now = f.preview ? static_cast<std::uint64_t>(ImGui::GetTime() * 1e9) : (m_source ? m_source->nowNs() : 0);
    const auto trail = [&](const gamepad::StickTrail& t, const ImVec2 rest, const Color& color) {
        const auto& points = t.points();
        for (std::size_t i = 1; i < points.size(); ++i) {
            const auto& a = points[i - 1];
            const auto& b = points[i];
            const float age = static_cast<float>(now > b.timestampNs ? now - b.timestampNs : 0) / static_cast<float>(t.lengthNs());
            const auto alpha = static_cast<std::uint8_t>(std::clamp(1.0f - age, 0.0f, 1.0f) * 220.0f);
            draw->AddLine(rest + ImVec2(a.x, -a.y) * layout.stickRange, rest + ImVec2(b.x, -b.y) * layout.stickRange, toImU32(color.withAlpha(alpha)), 3.0f);
        }
    };
    trail(m_leftTrail, layout.leftStickRest, p.accentText);
    trail(m_rightTrail, layout.rightStickRest, Color{0x38, 0xBD, 0xF8});

    // In the preview, the keys behind each part are one hover away.
    ImGui::InvisibleButton("##art", ImVec2(artWidth, artHeight));
    if (ImGui::IsItemHovered() && f.preview) {
        if (const auto part = art::hit(layout, ImGui::GetIO().MousePos)) {
            std::string keys;
            for (const auto& [key, target] : gamepadKeys(f.details)) {
                const auto where = partOfTarget(target);
                if (!where || where->first != *part) continue;
                if (!keys.empty()) keys += ", ";
                keys += widgets::keyName(key, f.layout) + " (" + mapping::targetName(target, f.labels) + ")";
            }
            ImGui::SetTooltip("%s", keys.empty() ? "No key drives this" : keys.c_str());
        }
    }
    drawReadouts(m_pad, artWidth, f, p);
    ImGui::EndGroup();
    const float leftHeight = ImGui::GetItemRectSize().y;

    ImGui::SameLine(0.0f, em * 1.2f);
    drawHistory(ImGui::GetContentRegionAvail().x, leftHeight, f, p);
    ImGui::Spacing();
    drawStickTests(p);
}

void GamepadPanel::drawReadouts(const gamepad::PadState& pad, const float width, const Frame& f, const ThemePalette& p) {
    const float em = ImGui::GetFontSize();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const bool ps = f.labels == LabelStyle::PlayStation;
    struct Readout {
        const char* name;
        std::string value;
        Color       color;
    };
    const Readout values[] = {
        {"LS", signedValue(pad.axis(Axis::LeftX)) + "  " + signedValue(pad.axis(Axis::LeftY)), p.accentText},
        {"RS", signedValue(pad.axis(Axis::RightX)) + "  " + signedValue(pad.axis(Axis::RightY)), Color{0x38, 0xBD, 0xF8}},
        {ps ? "L2" : "LT", percent(pad.axis(Axis::LeftTrigger)), p.warning},
        {ps ? "R2" : "RT", percent(pad.axis(Axis::RightTrigger)), p.warning},
    };
    const float cell = (width - em * 0.6f * 3.0f) / 4.0f;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    for (std::size_t i = 0; i < std::size(values); ++i) {
        const ImVec2 min(at.x + static_cast<float>(i) * (cell + em * 0.6f), at.y);
        const ImVec2 max(min.x + cell, min.y + em * 2.1f);
        draw->AddRectFilled(min, max, toImU32(p.surfaceRaised.withAlpha(150)), em * 0.4f);
        draw->AddText(min + ImVec2(em * 0.5f, em * 0.5f), toImU32(values[i].color), values[i].name);
        const ImVec2 size = ImGui::CalcTextSize(values[i].value.c_str());
        draw->AddText(ImVec2(max.x - size.x - em * 0.5f, min.y + em * 0.5f), toImU32(p.text), values[i].value.c_str());
    }
    ImGui::Dummy(ImVec2(width, em * 2.3f));
}

void GamepadPanel::drawHistory(const float width, const float height, const Frame& f, const ThemePalette& p) {
    const LabelStyle style = f.labels;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toImVec4(p.surface));
    ImGui::PushStyleColor(ImGuiCol_Border, toImVec4(p.border));
    ImGui::BeginChild("##history", ImVec2(width, height), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const float em = ImGui::GetFontSize();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Input history");
    widgets::alignRight(widgets::buttonWidth("Clear"));
    if (widgets::secondaryButton("Clear", ImVec2(0, 0), p)) m_history.clear();
    static constexpr const char* NOTATIONS[] = {"Buttons", "Tekken", "SF"};
    const int picked = widgets::segmented("##notation", NOTATIONS, static_cast<int>(m_notation), p, ImGui::GetContentRegionAvail().x);
    if (picked >= 0) m_notation = static_cast<gamepad::Notation>(picked);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Buttons: the pad's names.\nTekken: 1 2 3 4 = X Y A B.\nSF: LP MP HP / LK MK HK = X Y RB / A B RT.");
    }
    ImGui::Spacing();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float row = em * 1.7f;
    const float bottom = ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y;
    const std::uint64_t now = f.preview ? static_cast<std::uint64_t>(ImGui::GetTime() * 1e9) : (m_source ? m_source->nowNs() : 0);
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    std::size_t index = 0;
    for (const gamepad::HistoryEntry& entry : m_history.entries()) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        if (at.y + row > bottom) break;
        const bool idle = entry.direction == 5 && !entry.anyHeld();
        if (idle && index > 0) {
            ++index;
            ImGui::Dummy(ImVec2(0, row * 0.35f)); // neutral frames as a small gap, so actions stand out
            continue;
        }
        const float fade = index == 0 ? 1.0f : std::max(0.45f, 1.0f - static_cast<float>(index) * 0.03f);
        const Color ink = (idle ? p.textMuted : p.text).withAlpha(static_cast<std::uint8_t>(255.0f * fade));
        draw->AddRectFilled(at, ImVec2(right, at.y + row - em * 0.15f), toImU32(p.surfaceRaised.withAlpha(index == 0 ? 160 : 70)), em * 0.3f);

        // Direction.
        const ImVec2 dirCentre(at.x + row * 0.5f, at.y + row * 0.45f);
        widgets::drawDirection(draw, entry.direction, dirCentre, row * 0.3f, toImU32(idle ? p.textMuted : p.accentText));

        // Buttons held (newly pressed ones filled).
        float x = at.x + row * 1.1f;
        for (const gamepad::Attack attack : ATTACKS) {
            const auto i = static_cast<std::size_t>(attack);
            if (!entry.held[i]) continue;
            const bool fresh = entry.pressed[i];
            const Color fill = fresh ? groupColor(TargetGroup::FaceButtons, p).mix(p.surface, 0.15f) : p.surfaceRaised.lighten(0.08f);
            const Color text = fresh ? p.background : p.text;
            const auto face = faceButtonOf(attack);
            if (face && m_notation == gamepad::Notation::PadLabels && m_workspace->glyphs != GlyphStyle::Drawn) {
                const auto target = faceTarget(*face);
                const float s = row * 0.8f;
                if (target && glyphs::drawTarget(draw, *target, style, ImVec2(x + s * 0.5f, at.y + row * 0.45f), s,
                                                 fresh ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 120),
                                                 m_workspace->glyphs == GlyphStyle::Zacksly ? glyphs::Pack::Zacksly : glyphs::Pack::Kenney)) {
                    x += s + em * 0.3f;
                    continue;
                }
            }
            if (face && m_notation == gamepad::Notation::PadLabels && style == LabelStyle::PlayStation) {
                const float r = row * 0.34f;
                widgets::drawFaceButton(draw, *face, style, ImVec2(x + r, at.y + row * 0.45f), r, toImU32(fill), toImU32(text));
                x += r * 2.0f + em * 0.3f;
                continue;
            }
            const char* label = gamepad::attackLabel(attack, m_notation, style);
            if (label == nullptr) label = gamepad::attackLabel(attack, gamepad::Notation::PadLabels, style);
            x += chip(draw, ImVec2(x, at.y + row * 0.1f), label, fill, text, row * 0.7f) + em * 0.3f;
        }

        // Frames held.
        const std::uint32_t frames = m_history.frames(entry, now);
        char count[16];
        std::snprintf(count, sizeof(count), frames > 999 ? "999+" : "%u", frames);
        const ImVec2 size = ImGui::CalcTextSize(count);
        draw->AddText(ImVec2(right - size.x - em * 0.4f, at.y + (row - em * 0.15f - size.y) * 0.5f), toImU32(ink), count);

        ImGui::Dummy(ImVec2(0, row));
        ++index;
    }
    if (m_history.entries().empty()) widgets::mutedWrapped("Press something: directions, buttons and how many frames (60 per second) each was held.", p);
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

void GamepadPanel::drawStickTests(const ThemePalette& p) {
    if (!widgets::beginCard("##sticktests", p)) {
        widgets::endCard();
        return;
    }
    const float em = ImGui::GetFontSize();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Stick check");
    widgets::alignRight(widgets::buttonWidth("Reset"));
    if (widgets::secondaryButton("Reset", ImVec2(0, 0), p)) {
        m_leftTest.reset();
        m_rightTest.reset();
    }
    widgets::mutedWrapped("Roll each stick around its edge. A good controller scores a few % error. The keyboard reaches the "
                          "edge of the circle on diagonals too, because the firmware scales W + D back onto it.", p);

    const float size = em * 8.0f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const auto circle = [&](const char* name, const gamepad::CircularityTest& test, const float x, const float y, const Color& color) {
        ImGui::BeginGroup();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 c(origin.x + size * 0.5f, origin.y + size * 0.5f);
        const float r = size * 0.36f;
        draw->AddCircleFilled(c, size * 0.48f, toImU32(p.background.mix(p.surface, 0.4f)));
        draw->AddCircle(c, r, toImU32(p.textMuted.withAlpha(160)), 0, 1.0f);
        draw->AddLine(ImVec2(c.x - size * 0.48f, c.y), ImVec2(c.x + size * 0.48f, c.y), toImU32(p.border));
        draw->AddLine(ImVec2(c.x, c.y - size * 0.48f), ImVec2(c.x, c.y + size * 0.48f), toImU32(p.border));
        std::vector<ImVec2> outline;
        const auto& radii = test.radii();
        for (std::size_t i = 0; i < radii.size(); ++i) {
            if (radii[i] <= 0.0f) continue;
            const float angle = (static_cast<float>(i) + 0.5f) / static_cast<float>(radii.size()) * 2.0f * std::numbers::pi_v<float> - std::numbers::pi_v<float>;
            const float rr = std::min(radii[i], 1.3f) * r;
            outline.emplace_back(c.x + std::cos(angle) * rr, c.y - std::sin(angle) * rr);
        }
        if (outline.size() >= 3) draw->AddPolyline(outline.data(), static_cast<int>(outline.size()), toImU32(color), ImDrawFlags_Closed, 2.0f);
        draw->AddCircleFilled(ImVec2(c.x + x * r, c.y - y * r), em * 0.28f, toImU32(p.text));
        ImGui::Dummy(ImVec2(size, size));
        ImGui::TextUnformatted(name);
        const auto error = test.averageErrorPercent();
        char line[64];
        if (error) {
            std::snprintf(line, sizeof(line), "%.1f %% error, %.0f %% covered", static_cast<double>(*error), static_cast<double>(test.coverage() * 100.0f));
        } else {
            std::snprintf(line, sizeof(line), "not measured yet");
        }
        ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(p.textMuted));
        ImGui::TextUnformatted(line);
        ImGui::PopStyleColor();
        ImGui::EndGroup();
    };
    circle("Left stick", m_leftTest, m_pad.axis(Axis::LeftX), m_pad.axis(Axis::LeftY), p.accentText);
    ImGui::SameLine(0.0f, em * 2.0f);
    circle("Right stick", m_rightTest, m_pad.axis(Axis::RightX), m_pad.axis(Axis::RightY), Color{0x38, 0xBD, 0xF8});
    widgets::endCard();
}

// -----------------------------------------------------------------------------
// Key mapping
// -----------------------------------------------------------------------------
std::optional<std::uint8_t> GamepadPanel::targetForPart(const art::Part part, const ImVec2 click, const art::Layout& layout) const {
    const auto stick = [&](const bool left) -> std::uint8_t {
        const ImVec2 c = left ? layout.leftStickRest : layout.rightStickRest;
        const ImVec2 d = click - c;
        const float radius = layout.extent[static_cast<std::size_t>(left ? art::Part::LeftStick : art::Part::RightStick)].x;
        if (std::hypot(d.x, d.y) < radius * 0.38f) return GT::button(left ? 8 : 9);
        const std::uint8_t base = left ? GT::LEFT_STICK_LEFT : GT::RIGHT_STICK_LEFT;
        if (std::fabs(d.x) > std::fabs(d.y)) return static_cast<std::uint8_t>(base + (d.x < 0 ? 0 : 1));
        return static_cast<std::uint8_t>(base + (d.y > 0 ? 2 : 3));
    };
    switch (part) {
        case art::Part::A:          return GT::button(0);
        case art::Part::B:          return GT::button(1);
        case art::Part::X:          return GT::button(2);
        case art::Part::Y:          return GT::button(3);
        case art::Part::LB:         return GT::button(4);
        case art::Part::RB:         return GT::button(5);
        case art::Part::View:       return GT::button(6);
        case art::Part::Menu:       return GT::button(7);
        case art::Part::DpadUp:     return GT::button(10);
        case art::Part::DpadDown:   return GT::button(11);
        case art::Part::DpadLeft:   return GT::button(12);
        case art::Part::DpadRight:  return GT::button(13);
        case art::Part::Guide:      return GT::button(14);
        case art::Part::LT:         return GT::LEFT_TRIGGER;
        case art::Part::RT:         return GT::RIGHT_TRIGGER;
        case art::Part::LeftStick:  return stick(true);
        case art::Part::RightStick: return stick(false);
        case art::Part::Count:      break;
    }
    return std::nullopt;
}

void GamepadPanel::assign(const Frame& f, const std::optional<std::uint8_t> target) {
    for (const KeyPosition& key : m_selection) {
        if (!isKeyOnBoard(key)) continue;
        const KeyAdvanced stored = f.details.at(key);
        const bool storedIsTarget = target ? (stored.mode == kbd::AdvancedMode::GAMEPAD && stored.data == *target)
                                           : stored.mode != kbd::AdvancedMode::GAMEPAD;
        if (storedIsTarget) {
            m_draft.erase(key);
        } else {
            m_draft[key] = target;
        }
    }
}

void GamepadPanel::drawMapping(const Frame& f, const ThemePalette& p) {
    const LabelStyle style = f.labels;
    const auto liveOf = [&](const KeyPosition& key) -> std::optional<float> {
        for (const LiveKeyTravel& reading : m_live) {
            if (reading.key == key) return static_cast<float>(reading.travelRaw) / static_cast<float>(kbd::model::TRAVEL_SCALE);
        }
        return std::nullopt;
    };

    const auto display = [&](const KeyPosition& key) {
        KeyDisplay shown;
        shown.hideValue = true;
        if (const auto depth = liveOf(key)) shown.level = *depth / 40.0f;
        const auto target = targetOf(f.details, key);
        const bool edited = m_draft.contains(key);
        if (!target) {
            const KeyAdvanced advanced = f.details.at(key);
            if (advanced.mode == kbd::AdvancedMode::DKS) {
                shown.dim  = true;
                shown.note = "Dynamic keystroke: see the Movement page.";
            }
            if (edited) shown.note = "Becomes a normal key when you apply.";
            return shown;
        }
        const mapping::TargetInfo* info = mapping::targetInfo(*target);
        const Color color = info ? groupColor(info->group, p) : p.textMuted;
        KeyBadge badge{info ? info->chip : "?", info ? info->glyph : KeyGlyph::None, color, p.background, std::nullopt, LabelStyle::Xbox, std::nullopt, false};
        if (info && info->group == TargetGroup::FaceButtons && info->button && style == LabelStyle::PlayStation) {
            badge.faceButton = info->button;
            badge.style      = style;
        }
        if (info && info->group == TargetGroup::Triggers && style == LabelStyle::PlayStation) {
            badge.text = info->axis == Axis::LeftTrigger ? "L2" : "R2";
        }
        if (m_workspace->glyphs != GlyphStyle::Drawn) {
            badge.prompt  = *target;
            badge.style   = style;
            badge.zacksly = m_workspace->glyphs == GlyphStyle::Zacksly;
        }
        shown.badge = badge;
        shown.tint  = color;
        shown.note  = mapping::targetName(*target, style) + (edited ? "  (not applied yet)" : "");
        return shown;
    };
    const KeyboardView::Result result = m_keyboard.draw("##gamepad_keys", keyboardLayout(f.layout), display, m_selection, p, ImGui::GetContentRegionAvail().x);
    m_hoveredKey = result.hovered;
    ImGui::Spacing();

    // Controller (click a part to bind) on the left, the target list on the right.
    const float em = ImGui::GetFontSize();
    const float avail = ImGui::GetContentRegionAvail().x;
    const float artWidth = std::floor(std::min(avail * 0.5f, em * 32.0f));
    if (!widgets::beginCard("##mapping_card", p)) {
        widgets::endCard();
        return;
    }
    ImGui::BeginGroup();
    // Wrap text at the drawing's width, so the group (and the list beside it) keeps its column.
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + artWidth);
    std::string heading = m_selection.empty() ? std::string("Select keys, then click what they should drive")
                                              : "Click a part to bind " + std::to_string(m_selection.size()) + (m_selection.size() == 1 ? " key" : " keys");
    ImGui::TextUnformatted(heading.c_str());
    const ImVec2 origin = ImGui::GetCursorScreenPos() + ImVec2(0, em * 0.4f);

    std::array<bool, art::PART_COUNT> emphasis{};
    if (m_hoveredKey) {
        if (const auto target = targetOf(f.details, *m_hoveredKey)) {
            if (const auto where = partOfTarget(*target)) emphasis[static_cast<std::size_t>(where->first)] = true;
        }
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const art::Layout layout = art::draw(art::modelFor(f.labels), draw, origin, artWidth, m_pad, artStyle(p), emphasis);
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##art", layout.max - layout.min);
    const bool artHovered = ImGui::IsItemHovered();
    const bool artClicked = ImGui::IsItemClicked();
    if (artHovered) {
        if (const auto part = art::hit(layout, ImGui::GetIO().MousePos)) {
            const ImVec2 c = layout.centre[static_cast<std::size_t>(*part)];
            const ImVec2 e = layout.extent[static_cast<std::size_t>(*part)];
            draw->AddEllipse(c, e + ImVec2(3, 3), toImU32(p.accentText), 0.0f, 40, 2.0f);
            if (const auto target = targetForPart(*part, ImGui::GetIO().MousePos, layout)) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::SetTooltip("%s%s", mapping::targetName(*target, style).c_str(), m_selection.empty() ? "\n(select keys first)" : "");
                if (artClicked && !m_selection.empty()) assign(f, target);
            }
        }
    }

    // Key names next to the parts they drive, laid out so they never overlap.
    drawPartLabels(draw, layout, f, p);
    ImGui::SetCursorScreenPos(ImVec2(origin.x, layout.max.y + em * 0.6f));
    widgets::mutedWrapped("Sticks: click the side of the stick for a direction, its centre for the stick click. Yellow labels "
                          "show which keys drive each part (unapplied edits included).", p);
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, em * 1.5f);
    ImGui::BeginGroup();
    drawPalette(f, p);
    ImGui::EndGroup();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    drawApplyBar(f, p);
    widgets::endCard();
}

void GamepadPanel::drawPalette(const Frame& f, const ThemePalette& p) {
    const LabelStyle style = f.labels;
    const float em = ImGui::GetFontSize();
    const float width = ImGui::GetContentRegionAvail().x;

    widgets::sectionLabel("Or pick from the list", p);
    ImGui::BeginDisabled(m_selection.empty());
    const TargetGroup order[] = {TargetGroup::LeftStick, TargetGroup::RightStick, TargetGroup::Dpad, TargetGroup::Triggers,
                                 TargetGroup::FaceButtons, TargetGroup::Shoulders, TargetGroup::Menu, TargetGroup::StickClicks};
    const float labelWidth = em * 7.0f;
    const float rowStart = ImGui::GetCursorScreenPos().x;
    const float rowEnd = rowStart + width - em * 0.5f;
    for (const TargetGroup group : order) {
        ImGui::PushID(static_cast<int>(group));
        ImGui::AlignTextToFramePadding();
        const Color color = groupColor(group, p);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(at.x + em * 0.3f, at.y + ImGui::GetFrameHeight() * 0.5f), em * 0.24f, toImU32(color));
        ImGui::Dummy(ImVec2(em * 0.8f, 0));
        ImGui::SameLine();
        ImGui::TextUnformatted(mapping::groupName(group));
        ImGui::SameLine(labelWidth);
        bool first = true;
        for (const mapping::TargetInfo& info : mapping::targets()) {
            if (info.group != group) continue;
            std::string label;
            if (group == TargetGroup::LeftStick || group == TargetGroup::RightStick || group == TargetGroup::Dpad) {
                label = info.glyph == KeyGlyph::ArrowUp ? "Up" : info.glyph == KeyGlyph::ArrowDown ? "Down" : info.glyph == KeyGlyph::ArrowLeft ? "Left" : "Right";
            } else {
                label = mapping::targetName(info.target, style);
            }
            // With a prompt set chosen, the button carries the prompt glyph in front of its name.
            const bool prompt = m_workspace->glyphs != GlyphStyle::Drawn && glyphs::hasGlyph(info.target);
            const float icon = ImGui::GetFrameHeight();
            // Most prompts say it all (arrows, letters, shapes, LB / LT): those buttons are the glyph alone, named in
            // the tooltip. Menu buttons and stick clicks keep their names, their icons alone are ambiguous.
            const bool iconOnly = prompt && group != TargetGroup::Menu && group != TargetGroup::StickClicks;
            const std::string text = iconOnly ? "     ##" + std::to_string(info.target)
                                              : (prompt ? std::string("     ") : std::string()) + label + "##" + std::to_string(info.target);
            // Next to the previous button, or on a new line under it when the row is full.
            if (!first) {
                ImGui::SameLine(0.0f, em * 0.25f);
                if (ImGui::GetCursorScreenPos().x + widgets::buttonWidth(text.c_str()) > rowEnd) {
                    ImGui::NewLine();
                    ImGui::SetCursorScreenPos(ImVec2(rowStart + labelWidth, ImGui::GetCursorScreenPos().y));
                }
            }
            first = false;
            if (widgets::secondaryButton(text.c_str(), ImVec2(0, 0), p)) assign(f, info.target);
            if (prompt) {
                const ImVec2 min = ImGui::GetItemRectMin();
                const float cx = iconOnly ? (min.x + ImGui::GetItemRectMax().x) * 0.5f : min.x + icon * 0.62f;
                glyphs::drawTarget(ImGui::GetWindowDrawList(), info.target, style, ImVec2(cx, min.y + icon * 0.5f), icon * 0.92f,
                                   toImU32(m_selection.empty() ? p.textMuted : p.text),
                                   m_workspace->glyphs == GlyphStyle::Zacksly ? glyphs::Pack::Zacksly : glyphs::Pack::Kenney);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", mapping::targetName(info.target, style).c_str());
        }
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(em * 0.8f, 0));
    ImGui::SameLine(labelWidth);
    if (widgets::secondaryButton("Normal key", ImVec2(0, 0), p)) assign(f, std::nullopt);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Take the selected keys off the controller");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(em * 8.0f, width - ImGui::GetCursorPosX() + ImGui::GetWindowPos().x - em));
    if (ImGui::BeginCombo("##hidonly", "HID joystick only...")) {
        for (const mapping::TargetInfo& info : mapping::targets()) {
            if (info.group != TargetGroup::HidOnly) continue;
            if (ImGui::Selectable(info.name)) assign(f, info.target);
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    widgets::sectionLabel("Presets (only change the keys they list)", p);
    const float presetsEnd = ImGui::GetCursorScreenPos().x + width;
    bool first = true;
    for (const mapping::Preset& preset : mapping::presets()) {
        ImGui::PushID(preset.name);
        // Wrap to a new line instead of running past the card.
        if (!first) {
            ImGui::SameLine();
            if (ImGui::GetCursorScreenPos().x + widgets::buttonWidth(preset.name) > presetsEnd) ImGui::NewLine();
        }
        first = false;
        if (widgets::secondaryButton(preset.name, ImVec2(0, 0), p)) {
            m_selection.clear();
            for (const auto& [key, target] : preset.bindings) {
                m_draft[key] = target;
                m_selection.insert(key);
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", preset.description);
        ImGui::PopID();
    }
}

void GamepadPanel::drawApplyBar(const Frame& f, const ThemePalette& p) {
    const std::string pending = m_draft.empty() ? "No unapplied changes" : std::to_string(m_draft.size()) + (m_draft.size() == 1 ? " key changed" : " keys changed");
    const auto changes = [&] {
        std::vector<AdvancedKeyChange> list;
        for (const auto& [key, target] : m_draft) {
            AdvancedKeyChange change;
            change.key   = key;
            change.kind  = target ? AdvancedKeyChange::Kind::Gamepad : AdvancedKeyChange::Kind::Clear;
            change.index = target.value_or(0);
            list.push_back(change);
        }
        return list;
    };
    ImGui::BeginDisabled(!f.canAct || m_draft.empty());
    if (widgets::primaryButton(("Apply and save to " + slotLabel(f.slot)).c_str(), ImVec2(0, 0), p)) {
        m_commands.applyAdvancedKeys(static_cast<std::uint8_t>(f.slot), changes(), true);
    }
    ImGui::SameLine();
    if (widgets::secondaryButton("Try", ImVec2(0, 0), p)) m_commands.applyAdvancedKeys(static_cast<std::uint8_t>(f.slot), changes(), false);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Send to the keyboard without saving (lost on replug)");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_draft.empty());
    if (widgets::secondaryButton("Revert", ImVec2(0, 0), p)) m_draft.clear();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(m_draft.empty() ? p.textMuted : p.warning));
    ImGui::TextUnformatted(pending.c_str());
    ImGui::PopStyleColor();
}

// -----------------------------------------------------------------------------
// Response curve
// -----------------------------------------------------------------------------
void GamepadPanel::drawCurve(const Frame& f, const ThemePalette& p) {
    const AppSnapshot& state = *f.state;
    const float em = ImGui::GetFontSize();
    if (!m_curveEdited && state.controller.curve) m_curveDraft = *state.controller.curve;

    // Keys of the slot that drive a stick: the live marker follows one of them.
    std::vector<std::pair<KeyPosition, std::uint8_t>> stickKeys;
    for (const auto& [key, target] : gamepadKeys(f.details)) {
        const mapping::TargetInfo* info = mapping::targetInfo(target);
        if (info && (info->group == TargetGroup::LeftStick || info->group == TargetGroup::RightStick)) stickKeys.emplace_back(key, target);
    }
    if (m_curveKey && std::none_of(stickKeys.begin(), stickKeys.end(), [&](const auto& k) { return k.first == *m_curveKey; })) m_curveKey.reset();
    if (!m_curveKey && !stickKeys.empty()) m_curveKey = stickKeys.front().first;

    if (!widgets::beginCard("##curve_card", p)) {
        widgets::endCard();
        return;
    }
    const float avail = ImGui::GetContentRegionAvail().x;
    const float plotWidth = std::floor(std::min(avail - em * 17.0f, em * 40.0f));
    const float plotHeight = std::floor(plotWidth * 0.55f);

    std::optional<float> liveTenths;
    for (const LiveKeyTravel& reading : m_live) {
        if (m_curveKey && reading.key == *m_curveKey) liveTenths = static_cast<float>(reading.travelRaw) / static_cast<float>(kbd::model::TRAVEL_SCALE);
    }
    std::optional<float> reported;
    if (m_curveKey) {
        const auto it = std::find_if(stickKeys.begin(), stickKeys.end(), [&](const auto& k) { return k.first == *m_curveKey; });
        const mapping::TargetInfo* info = it != stickKeys.end() ? mapping::targetInfo(it->second) : nullptr;
        if (info && info->axis) reported = std::clamp(m_pad.axis(*info->axis) * static_cast<float>(info->axisSign), 0.0f, 1.0f);
    }
    if (plots::curvePlot("##curve", m_curveDraft, liveTenths, reported, ImVec2(plotWidth, plotHeight), p)) m_curveEdited = true;

    ImGui::SameLine(0.0f, em * 1.2f);
    ImGui::BeginGroup();
    widgets::sectionLabel("Presets", p);
    for (std::size_t i = 0; i < std::size(CURVE_PRESETS); ++i) {
        if (i % 2 == 1) ImGui::SameLine();
        if (widgets::secondaryButton(CURVE_PRESETS[i].name, ImVec2(em * 7.0f, 0), p)) {
            m_curveDraft  = CURVE_PRESETS[i].curve;
            m_curveEdited = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", CURVE_PRESETS[i].tip);
    }
    ImGui::Spacing();
    widgets::sectionLabel("Points", p);
    for (std::size_t i = 0; i < 4; ++i) {
        ImGui::PushID(static_cast<int>(i));
        int x = m_curveDraft[i].x;
        int y = static_cast<int>(std::lround(static_cast<float>(m_curveDraft[i].y) / 127.0f * 100.0f));
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%zu", i + 1);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(em * 6.0f);
        const int low  = i == 0 ? 0 : m_curveDraft[i - 1].x;
        const int high = i == 3 ? kbd::CURVE_MAX_X : m_curveDraft[i + 1].x;
        char xLabel[16];
        std::snprintf(xLabel, sizeof(xLabel), "%.1f mm", static_cast<double>(x) / 10.0);
        if (ImGui::DragInt("##x", &x, 0.15f, low, high, xLabel, ImGuiSliderFlags_AlwaysClamp)) {
            m_curveDraft[i].x = static_cast<std::uint8_t>(x);
            m_curveEdited = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(em * 5.5f);
        if (ImGui::DragInt("##y", &y, 0.3f, 0, 100, "%d %%", ImGuiSliderFlags_AlwaysClamp)) {
            m_curveDraft[i].y = static_cast<std::uint8_t>(std::lround(static_cast<float>(y) / 100.0f * 127.0f));
            m_curveEdited = true;
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    const std::string follow = m_curveKey ? widgets::keyName(*m_curveKey, f.layout) : std::string("none");
    widgets::sectionLabel("Live key", p);
    ImGui::SetNextItemWidth(em * 13.0f);
    if (ImGui::BeginCombo("##curvekey", follow.c_str())) {
        for (const auto& [key, target] : stickKeys) {
            const std::string label = widgets::keyName(key, f.layout) + "  (" + mapping::targetName(target, f.labels) + ")";
            if (ImGui::Selectable(label.c_str(), m_curveKey && *m_curveKey == key)) m_curveKey = key;
        }
        if (stickKeys.empty()) ImGui::TextDisabled("No key of this slot drives a stick.");
        ImGui::EndCombo();
    }
    ImGui::Spacing();
    const bool valid = kbd::isValidCurve(m_curveDraft);
    const bool differs = !state.controller.curve || *state.controller.curve != m_curveDraft;
    ImGui::BeginDisabled(!f.canAct || !valid || !differs);
    if (widgets::primaryButton("Save curve", ImVec2(0, 0), p)) {
        m_commands.applyCurve(m_curveDraft);
        m_curveEdited = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_curveEdited);
    if (widgets::secondaryButton("Revert", ImVec2(0, 0), p)) m_curveEdited = false;
    ImGui::EndDisabled();
    ImGui::EndGroup();

    widgets::mutedWrapped("Drag the points. The curve is stored in the keyboard (one for all slots) and shapes the sticks; triggers "
                          "stay linear. Green: the live key. Blue: what the controller reports for it.", p);
    widgets::endCard();
}

void GamepadPanel::drawPartLabels(ImDrawList* draw, const art::Layout& layout, const Frame& f, const ThemePalette& p) const {
    const float em = ImGui::GetFontSize();
    const float chipH = em * 1.2f;

    // One label per (part, direction); several keys on the same spot share it ("W, Up", "W +2").
    std::map<std::pair<art::Part, int>, std::vector<std::string>> grouped;
    for (const auto& [key, target] : gamepadKeys(f.details)) {
        const auto where = partOfTarget(target);
        if (!where) continue;
        grouped[{where->first, labelDirection(where->first, where->second)}].push_back(widgets::keyName(key, f.layout));
    }
    if (grouped.empty()) return;

    // Centre of the face buttons, so their labels point away from the cluster.
    ImVec2 faceCentre(0, 0);
    for (const art::Part part : {art::Part::A, art::Part::B, art::Part::X, art::Part::Y}) {
        faceCentre += layout.centre[static_cast<std::size_t>(part)] * 0.25f;
    }

    std::vector<PartLabel> labels;
    for (const auto& [where, keys] : grouped) {
        std::string text = keys.front();
        if (keys.size() == 2) text += ", " + keys[1];
        if (keys.size() > 2) text += " +" + std::to_string(keys.size() - 1);
        const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        const ImVec2 size(textSize.x + em * 0.6f, chipH);
        const ImVec2 c = layout.centre[static_cast<std::size_t>(where.first)];
        const ImVec2 e = layout.extent[static_cast<std::size_t>(where.first)];
        ImVec2 dir(0.0f, -1.0f);
        switch (where.second) {
            case 2: dir = ImVec2(0.0f, 1.0f); break;
            case 4: dir = ImVec2(-1.0f, 0.0f); break;
            case 6: dir = ImVec2(1.0f, 0.0f); break;
            case 5: dir = ImVec2(0.0f, 0.0f); break;
            case 0: {
                const ImVec2 d = c - faceCentre;
                const float len = std::sqrt(d.x * d.x + d.y * d.y);
                dir = len > 0.5f ? ImVec2(d.x / len, d.y / len) : ImVec2(0.0f, -1.0f);
                break;
            }
            default: break;
        }
        const ImVec2 anchor(c.x + dir.x * e.x, c.y + dir.y * e.y);
        const ImVec2 centre(anchor.x + dir.x * (size.x * 0.5f + em * 0.2f), anchor.y + dir.y * (size.y * 0.5f + em * 0.15f));
        labels.push_back({std::move(text), anchor, centre, size});
    }

    separateLabels(labels, layout.min - ImVec2(em, em), layout.max + ImVec2(em, em), em * 0.2f);

    const ImU32 fill = toImU32(p.warning);
    for (const PartLabel& l : labels) {
        const ImVec2 min = l.centre - l.size * 0.5f;
        // A leader line when the chip had to move away from its part.
        const ImVec2 nearest(std::clamp(l.anchor.x, min.x, min.x + l.size.x), std::clamp(l.anchor.y, min.y, min.y + l.size.y));
        const ImVec2 d = nearest - l.anchor;
        if (d.x * d.x + d.y * d.y > em * em * 0.36f) {
            draw->AddLine(l.anchor, nearest, toImU32(p.warning.withAlpha(170)), 1.5f);
            draw->AddCircleFilled(l.anchor, 2.5f, fill);
        }
        draw->AddRectFilled(min, min + l.size, fill, chipH * 0.3f);
        const ImVec2 textSize = ImGui::CalcTextSize(l.text.c_str());
        draw->AddText(ImVec2(min.x + (l.size.x - textSize.x) * 0.5f, min.y + (l.size.y - textSize.y) * 0.5f), toImU32(p.background), l.text.c_str());
    }
}

} // namespace ui
