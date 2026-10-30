#pragma once

namespace ui {

    struct Theme;

    class IThemeObserver {
    public:
        virtual ~IThemeObserver() = default;

        // May be invoked from any thread (whichever thread called ThemeManager::setTheme / load).
        // Observers that talk to a thread-affine toolkit must hand the change over to their own thread.
        virtual void onThemeChanged(const Theme& theme) = 0;
    };

} // namespace ui
