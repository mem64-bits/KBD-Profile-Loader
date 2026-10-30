#pragma once

#include "ui/AppState.h"
#include "ui/Theme.h"

namespace ui {

    // One tab of the dashboard. Panels draw with ImGui on the UI thread and are handed an immutable copy of
    // the application state and the active theme every frame, so they never touch shared state directly.
    class IPanel {
    public:
        virtual ~IPanel() = default;

        [[nodiscard]] virtual const char* title() const noexcept = 0;
        virtual void render(const AppSnapshot& state, const Theme& theme) = 0;
    };

} // namespace ui
