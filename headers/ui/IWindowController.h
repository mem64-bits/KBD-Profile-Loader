#pragma once

namespace ui {

    // Show / hide control over the dashboard window. All calls are thread-safe; the window itself is
    // only ever touched by the UI thread.
    class IWindowController {
    public:
        virtual ~IWindowController() = default;

        virtual void showDashboard() = 0;
        virtual void hideDashboard() = 0;
        virtual void toggleDashboard() = 0;
        [[nodiscard]] virtual bool isDashboardVisible() const noexcept = 0;
    };

} // namespace ui
