#pragma once

#include "ui/imgui/IPanel.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace ui {

    // The "Keyboard" page: everything that edits the selected slot (Actuation, Movement, Gamepad) as tabs under
    // one sidebar entry, like the Launcher's HE Mode page. Each tab is an ordinary IPanel, so the tabs keep
    // their own state and only the visible one polls the keyboard.
    class KeyboardPage final : public IPanel {
    public:
        void addTab(std::unique_ptr<IPanel> tab);
        void selectTab(std::size_t index);

        [[nodiscard]] const char* title() const noexcept override { return "Keyboard"; }
        [[nodiscard]] const char* subtitle() const noexcept override;
        [[nodiscard]] widgets::Icon icon() const noexcept override { return widgets::Icon::Actuation; }
        [[nodiscard]] bool editsSlot() const noexcept override { return true; }

        void render(const AppSnapshot& state, const Theme& theme) override;
        void onShown() override;
        void onHidden() override;

    private:
        std::vector<std::unique_ptr<IPanel>> m_tabs;
        std::size_t                          m_current{0};
        bool                                 m_shown{false};
    };

} // namespace ui
