#pragma once

#include "ui/ActuationModel.h"
#include "ui/IProfileCommands.h"
#include "ui/imgui/IPanel.h"

#include <array>
#include <optional>

namespace ui {

    // "Actuation" tab: whole-keyboard actuation point, Rapid Trigger and its sensitivities for one slot.
    // Edits stay in a per-slot draft until the user applies them; the draft follows what the keyboard
    // reports as long as the user has no unapplied changes of their own.
    class ActuationPanel final : public IPanel {
    public:
        explicit ActuationPanel(IProfileCommands& commands);
        ~ActuationPanel() override;

        ActuationPanel(const ActuationPanel&) = delete;
        ActuationPanel& operator=(const ActuationPanel&) = delete;
        ActuationPanel(ActuationPanel&&) = delete;
        ActuationPanel& operator=(ActuationPanel&&) = delete;

        [[nodiscard]] const char* title() const noexcept override;
        void render(const AppSnapshot& state, const Theme& theme) override;

    private:
        struct Draft {
            ActuationSettings values;
            ActuationSettings baseline;
            bool              initialised{false};
        };

        // What the keyboard (last read / applied), else the profile file, else the firmware defaults say.
        [[nodiscard]] static ActuationSettings baselineFor(const AppSnapshot& state, std::size_t slot);

        IProfileCommands&            m_commands;
        std::array<Draft, SLOT_COUNT> m_drafts;
        std::optional<std::size_t>   m_pickedSlot; // nullopt: follow the keyboard's active slot
    };

} // namespace ui
