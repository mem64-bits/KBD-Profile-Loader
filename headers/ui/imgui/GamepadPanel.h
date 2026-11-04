#pragma once

#include "kbd/FirmwareModel.h"
#include "ui/GamepadModel.h"
#include "ui/IProfileCommands.h"
#include "ui/imgui/ControllerArt.h"
#include "ui/imgui/IPanel.h"
#include "ui/imgui/KeyboardView.h"
#include "ui/imgui/Workspace.h"

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace ui {

    // The "Gamepad" page: the K2 HE's built-in analog controller for the workspace's slot.
    //
    // A slot is a "gamepad profile" when some of its keys drive the controller (key_config adv_mode GAMEPAD).
    // That is what the firmware stores per slot, so the page derives the state from the slot itself and never
    // keeps a separate flag that could drift. Turning a slot into a gamepad profile applies a starting preset and
    // makes sure the keyboard-wide XInput mode is on, walking through the replug the firmware needs for that.
    //
    //   Controller  - an Xbox Series-style controller showing either what Windows receives (SDL3) or a preview
    //                 computed from the live key travel with the firmware's own XInput maths, plus a fighting-game
    //                 input history and stick checks.
    //   Mapping     - which key drives which stick direction, trigger or button: pick keys, then click the part
    //                 on the controller (or the list); presets; unapplied edits already show in the preview.
    //   Curve       - the keyboard-wide 4-point travel -> stick curve, dragged on a plot with the live key on it.
    class GamepadPanel final : public IPanel {
    public:
        GamepadPanel(IProfileCommands& commands, std::shared_ptr<gamepad::IGamepadSource> source, std::shared_ptr<Workspace> workspace);

        [[nodiscard]] const char* title() const noexcept override;
        [[nodiscard]] const char* subtitle() const noexcept override;
        [[nodiscard]] widgets::Icon icon() const noexcept override;
        [[nodiscard]] bool editsSlot() const noexcept override { return true; }

        void render(const AppSnapshot& state, const Theme& theme) override;
        void onShown() override;
        void onHidden() override;

        enum class View : std::uint8_t { Controller, Mapping, Curve };
        void showView(View view) noexcept { m_view = view; }

    private:
        enum class Source : std::uint8_t { Auto, Windows, Preview };
        enum class Replug : std::uint8_t { None, Unplug, PlugIn, Verify };

        // Context for one frame.
        struct Frame {
            const AppSnapshot* state{nullptr};
            std::size_t        slot{0};
            LayoutKind         layout{LayoutKind::Iso};
            bool               canAct{false};
            SlotDetails        details;           // as stored
            std::size_t        padKeys{0};        // gamepad keys with the unapplied edits
            bool               keyboardPad{false}; // SDL sees the keyboard's own controller
            bool               preview{false};    // drawing the computed preview instead of SDL
            gamepad::LabelStyle labels{gamepad::LabelStyle::Xbox};
        };

        void consumeSdl(const Frame& f);
        void runPreview(const Frame& f);
        void pushState(const gamepad::PadState& pad, std::uint64_t nowNs);
        void watch(std::vector<KeyPosition> keys);

        void drawModeCard(const Frame& f, const ThemePalette& p);
        void drawSetup(const Frame& f, const ThemePalette& p);
        void drawReplug(const Frame& f, const ThemePalette& p);

        void drawController(const Frame& f, const ThemePalette& p);
        void drawSourceLine(const Frame& f, const ThemePalette& p);
        void drawReadouts(const gamepad::PadState& pad, float width, const Frame& f, const ThemePalette& p);
        void drawHistory(float width, float height, const Frame& f, const ThemePalette& p);
        void drawStickTests(const ThemePalette& p);

        void drawMapping(const Frame& f, const ThemePalette& p);
        void drawPartLabels(ImDrawList* draw, const art::Layout& layout, const Frame& f, const ThemePalette& p) const;
        void drawPalette(const Frame& f, const ThemePalette& p);
        void drawApplyBar(const Frame& f, const ThemePalette& p);
        void assign(const Frame& f, std::optional<std::uint8_t> target);

        void drawCurve(const Frame& f, const ThemePalette& p);

        // A key's gamepad target with the unsent edits applied (nullopt: not a gamepad key).
        [[nodiscard]] std::optional<std::uint8_t> targetOf(const SlotDetails& details, const KeyPosition& key) const;
        // Every gamepad key of the slot (edits applied), row by row like the firmware scans them.
        [[nodiscard]] std::vector<std::pair<KeyPosition, std::uint8_t>> gamepadKeys(const SlotDetails& details) const;
        // The target a click on a controller part means (sticks: the direction clicked; centre: the stick click).
        [[nodiscard]] std::optional<std::uint8_t> targetForPart(art::Part part, ImVec2 click, const art::Layout& layout) const;
        [[nodiscard]] art::Style artStyle(const ThemePalette& p) const;

        IProfileCommands&                        m_commands;
        std::shared_ptr<gamepad::IGamepadSource> m_source;
        std::shared_ptr<Workspace>               m_workspace;
        View                                     m_view{View::Controller};
        Source                                   m_sourceMode{Source::Auto};

        // What the controller does right now (SDL or preview) and its history.
        gamepad::PadState        m_pad;
        gamepad::InputHistory    m_history;
        gamepad::Notation        m_notation{gamepad::Notation::PadLabels};
        gamepad::StickTrail      m_leftTrail;
        gamepad::StickTrail      m_rightTrail;
        gamepad::CircularityTest m_leftTest;
        gamepad::CircularityTest m_rightTest;
        kbd::model::XInputModel  m_previewModel;
        bool                     m_lastWasPreview{false};

        // Mapping
        KeyboardView                                       m_keyboard;
        std::set<KeyPosition>                              m_selection;
        std::map<KeyPosition, std::optional<std::uint8_t>> m_draft;     // key -> new target (nullopt: plain key)
        std::size_t                                        m_draftSlot{SLOT_COUNT};
        std::optional<KeyPosition>                         m_hoveredKey;

        // Setup / controller mode
        std::size_t                 m_setupPreset{0};
        bool                        m_setupTyping{false};
        Replug                      m_replug{Replug::None};
        double                      m_replugSince{0.0};
        bool                        m_confirmOff{false};
        bool                        m_controllerRead{false};

        // Response curve
        kbd::ResponseCurve         m_curveDraft{kbd::DEFAULT_CURVE};
        bool                       m_curveEdited{false};
        std::optional<KeyPosition> m_curveKey;

        std::vector<KeyPosition>   m_watching;
        std::vector<LiveKeyTravel> m_live;
    };

} // namespace ui
