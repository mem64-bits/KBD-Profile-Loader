#pragma once

#include "ui/AppState.h"
#include "ui/Theme.h"
#include "ui/imgui/Widgets.h"

namespace ui {

    // One page of the dashboard. Panels draw with ImGui on the UI thread and are handed an immutable copy of
    // the application state and the active theme every frame, so they never touch shared state directly.
    class IPanel {
    public:
        virtual ~IPanel() = default;

        [[nodiscard]] virtual const char* title() const noexcept = 0;
        // One line under the page title.
        [[nodiscard]] virtual const char* subtitle() const noexcept { return ""; }
        [[nodiscard]] virtual widgets::Icon icon() const noexcept = 0;

        // True for pages that edit one slot: the page header then shows the slot switcher, and the dashboard
        // reads the slot from the keyboard the first time it is shown.
        [[nodiscard]] virtual bool editsSlot() const noexcept { return false; }

        virtual void render(const AppSnapshot& state, const Theme& theme) = 0;

        // The page was opened / left (only the selected page renders). Used to start and stop live polling.
        virtual void onShown() {}
        virtual void onHidden() {}
    };

} // namespace ui
