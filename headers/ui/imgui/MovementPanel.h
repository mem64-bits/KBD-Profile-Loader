#pragma once

#include "kbd/FirmwareModel.h"
#include "ui/IProfileCommands.h"
#include "ui/imgui/IPanel.h"
#include "ui/imgui/KeyboardView.h"
#include "ui/imgui/Workspace.h"

#include <array>
#include <deque>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace ui {

    // The "Movement" page: what WASD games need from a Hall-effect keyboard, for the workspace's slot.
    //   - SOCD pairs (Snap Tap): what the keyboard sends while two opposite keys are held, with the firmware's six
    //     modes per pair and a test bench that runs the firmware's own SOCD logic (kbd::model::SocdPairModel) on
    //     the live key travel, or on two simulated keys when no keyboard is connected.
    //   - Sprint on deep press: a dynamic keystroke that walks on a light press and also holds Shift (or another
    //     key) at the bottom, stored in the slot so it works in every game without the app running.
    class MovementPanel final : public IPanel {
    public:
        MovementPanel(IProfileCommands& commands, std::shared_ptr<Workspace> workspace);

        [[nodiscard]] const char* title() const noexcept override;
        [[nodiscard]] const char* subtitle() const noexcept override;
        [[nodiscard]] widgets::Icon icon() const noexcept override;
        [[nodiscard]] bool editsSlot() const noexcept override { return true; }

        void render(const AppSnapshot& state, const Theme& theme) override;
        void onHidden() override;

    private:
        // One frame of the SOCD test bench.
        struct BenchSample {
            double time{0.0};
            bool   down1{false};
            bool   down2{false};
            kbd::model::SocdOutput out;
        };

        void drawKeyboard(const SlotDetails& details, LayoutKind layout, const ThemePalette& p);
        void drawSocd(const AppSnapshot& state, std::size_t slot, bool canAct, LayoutKind layout, const ThemePalette& p);
        void drawSocdPairs(LayoutKind layout, const ThemePalette& p);
        void drawBench(const AppSnapshot& state, std::size_t slot, LayoutKind layout, float width, const ThemePalette& p);
        void drawSprint(std::size_t slot, bool canAct, LayoutKind layout, const SlotDetails& details, const ThemePalette& p);
        void updateWatch();

        [[nodiscard]] int socdPairOf(const KeyPosition& key) const noexcept; // -1 when the key is in no pair
        [[nodiscard]] bool canAddPair(const KeyPosition& a, const KeyPosition& b) const noexcept;
        [[nodiscard]] std::optional<float> liveTenths(const KeyPosition& key) const noexcept;

        IProfileCommands&              m_commands;
        std::shared_ptr<Workspace>     m_workspace;
        KeyboardView                   m_keyboard;
        std::set<KeyPosition>          m_selection;

        std::vector<kbd::SocdPair>     m_socdDraft;
        bool                           m_socdEdited{false};
        std::size_t                    m_draftSlot{SLOT_COUNT};
        std::size_t                    m_benchPair{0};

        // SOCD test bench
        bool                           m_simulate{false};      // drive the bench with the two sliders
        std::array<float, 2>           m_simTenths{};          // simulated key depths
        kbd::model::SocdPairModel      m_socdModel;
        std::array<kbd::model::KeyTriggerModel, 2> m_benchKeys;
        std::deque<BenchSample>        m_bench;
        std::optional<kbd::SocdPair>   m_benchFor;             // pair the bench state belongs to

        std::uint8_t                   m_shallowTenths{10}; // walk from 1.0 mm
        std::uint8_t                   m_deepTenths{30};    // run from 3.0 mm
        std::size_t                    m_extraKey{0};       // index into mapping::sprintKeys()

        std::vector<KeyPosition>       m_watching;
        std::vector<LiveKeyTravel>     m_live;
    };

} // namespace ui
