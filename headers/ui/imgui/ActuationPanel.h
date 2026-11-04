#pragma once

#include "ui/ActuationModel.h"
#include "ui/IProfileCommands.h"
#include "ui/KeyboardLayout.h"
#include "ui/imgui/IPanel.h"
#include "ui/imgui/KeyboardView.h"
#include "ui/imgui/Plots.h"
#include "ui/imgui/Workspace.h"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <set>

namespace ui {

    // "Actuation" page for the workspace's slot: the keyboard shows what every key does; under it one inspector
    // edits either the whole slot or the selected keys, next to a live key-travel diagram that shows exactly when
    // the keyboard registers and releases a press with those settings (the firmware's own logic). Edits stay in
    // drafts until applied; a draft follows what the keyboard reports while the user has no edits of their own.
    class ActuationPanel final : public IPanel {
    public:
        ActuationPanel(IProfileCommands& commands, std::shared_ptr<Workspace> workspace);
        ~ActuationPanel() override;

        ActuationPanel(const ActuationPanel&) = delete;
        ActuationPanel& operator=(const ActuationPanel&) = delete;
        ActuationPanel(ActuationPanel&&) = delete;
        ActuationPanel& operator=(ActuationPanel&&) = delete;

        [[nodiscard]] const char* title() const noexcept override;
        [[nodiscard]] const char* subtitle() const noexcept override;
        [[nodiscard]] widgets::Icon icon() const noexcept override;
        [[nodiscard]] bool editsSlot() const noexcept override { return true; }
        void render(const AppSnapshot& state, const Theme& theme) override;
        void onHidden() override;

    private:
        struct Draft {
            ActuationSettings values;
            ActuationSettings baseline;
            bool              initialised{false};
        };

        // Editor values for the selected keys: own settings, or "follow the slot".
        struct KeyDraft {
            bool              ownSettings{false};
            ActuationSettings values;
            bool              edited{false}; // the user changed something since it was loaded
        };

        enum class Tab : std::uint8_t { Slot, Keys };
        enum class Source { Keyboard, ProfileFile, Defaults };

        void drawKeyboard(const AppSnapshot& state, std::size_t slot, const ThemePalette& p);
        void drawInspector(const AppSnapshot& state, std::size_t slot, bool canAct, const ThemePalette& p);
        // Both return the settings the diagram should show.
        ActuationSettings drawSlotControls(const AppSnapshot& state, std::size_t slot, bool canAct, const ThemePalette& p);
        ActuationSettings drawKeyControls(const AppSnapshot& state, std::size_t slot, bool canAct, const ThemePalette& p);
        void drawDiagram(const ActuationSettings& settings, float width, float height, const ThemePalette& p);
        void syncKeyDraft(const AppSnapshot& state, std::size_t slot);
        void updateLiveKey();

        // What the keyboard (last read / applied), else the profile file, else the firmware defaults say.
        [[nodiscard]] static ActuationSettings baselineFor(const AppSnapshot& state, std::size_t slot);
        [[nodiscard]] static KeyMatrix keysFor(const AppSnapshot& state, std::size_t slot);
        [[nodiscard]] static Source keySource(const AppSnapshot& state, std::size_t slot);

        IProfileCommands&             m_commands;
        std::shared_ptr<Workspace>    m_workspace;
        std::array<Draft, SLOT_COUNT> m_drafts;
        KeyboardView                  m_keyboard;
        std::set<KeyPosition>         m_selection;
        KeyDraft                      m_keyDraft;
        std::size_t                   m_draftSlot{SLOT_COUNT}; // slot the key draft was loaded for
        Tab                           m_tab{Tab::Slot};
        bool                          m_hadSelection{false};

        // Live diagram: the first selected key's travel.
        std::optional<KeyPosition> m_liveKey;
        plots::TravelHistory       m_history;
    };

} // namespace ui
