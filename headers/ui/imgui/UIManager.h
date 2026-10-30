#pragma once

#include "ui/IThemeObserver.h"
#include "ui/IView.h"
#include "ui/IWindowController.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace ui {

    struct UIManagerOptions {
        std::string windowTitle{"Keychron K2 HE - Profile Loader"};
        int         width{480};
        int         height{700};
        bool        startVisible{false};
        int         exitAfterFrames{0}; // > 0: stop after this many rendered frames (previews, smoke tests)

        // Both run on the UI thread.
        std::function<void(int frame)>    onFrameRendered;     // after drawing, before the buffers are swapped
        std::function<void(bool visible)> onVisibilityChanged; // the window was shown or hidden
    };

    // Hosts the dashboard window: GLFW (Win32, X11 and Wayland) + OpenGL + Dear ImGui. The event loop runs
    // on the thread that calls run() - the main thread, as GLFW requires - and sleeps until something
    // happens, so a hidden window costs no CPU. Closing the window only hides it (the daemon lives on in
    // the tray). GLFW / ImGui types stay in the .cpp.
    class UIManager final : public IWindowController, public IThemeObserver {
    public:
        explicit UIManager(UIManagerOptions options = {});
        ~UIManager() override;

        UIManager(const UIManager&) = delete;
        UIManager& operator=(const UIManager&) = delete;
        UIManager(UIManager&&) = delete;
        UIManager& operator=(UIManager&&) = delete;

        void addView(std::shared_ptr<IView> view);

        // Runs the event loop until requestQuit(). Returns false when no window could be created (no display,
        // no OpenGL): the caller should carry on without a UI. Returns true after a normal run.
        bool run();
        void requestQuit() noexcept; // thread-safe

        // With no tray to bring the window back, the close button should end the program instead of hiding it.
        void setCloseButtonQuits(bool quits) noexcept;

        // Writes the current frame to a PNG. Only valid on the UI thread, from options.onFrameRendered.
        bool saveScreenshot(const std::filesystem::path& path);

        // --- IWindowController (thread-safe) ---
        void showDashboard() override;
        void hideDashboard() override;
        void toggleDashboard() override;
        [[nodiscard]] bool isDashboardVisible() const noexcept override;

        // --- IThemeObserver (any thread; the style is re-applied on the UI thread) ---
        void onThemeChanged(const Theme& theme) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace ui
