#pragma once

#include "ui/AppModel.h"
#include "ui/IProfileCommands.h"
#include "ui/ISystemIntegration.h"
#include "ui/IThemeObserver.h"
#include "ui/ITrayIcon.h"
#include "ui/IWindowController.h"

#include <cstdint>
#include <functional>
#include <mutex>

namespace ui {

    // Presenter between the application and any ITrayIcon backend. It observes the AppModel and the
    // theme to keep the tray's menu, tooltip and icon current, and it observes the tray to turn clicks
    // and menu choices into application commands. Contains no OS-specific code.
    class TrayPresenter : public IAppModelObserver, public IThemeObserver, public ITrayObserver {
    public:
        TrayPresenter(AppModel& model, ITrayIcon& tray, IProfileCommands& commands, ISystemIntegration& system,
                      IWindowController& window, std::function<void()> onQuit);
        ~TrayPresenter() override;

        TrayPresenter(const TrayPresenter&) = delete;
        TrayPresenter& operator=(const TrayPresenter&) = delete;
        TrayPresenter(TrayPresenter&&) = delete;
        TrayPresenter& operator=(TrayPresenter&&) = delete;

        // Pushes the current state to the tray, subscribes to model / theme / tray events and starts the
        // tray backend. Returns whether the tray icon is actually up (false: no tray in this session).
        bool start();
        void stop();

        // --- IAppModelObserver ---
        void onAppStateChanged(const AppSnapshot& snapshot) override;
        // --- IThemeObserver ---
        void onThemeChanged(const Theme& theme) override;
        // --- ITrayObserver ---
        void onTrayActivated() override;
        void onTraySecondaryActivated() override;
        void onTrayMenuItemSelected(int itemId) override;

    private:
        struct IconKey {
            int           activeSlot{-2};
            bool          connected{false};
            std::uint64_t themeRevision{~std::uint64_t{0}};

            bool operator==(const IconKey&) const = default;
        };

        void applyState(const AppSnapshot& state);
        void dispatch(TrayAction action);

        AppModel&             m_model;
        ITrayIcon&            m_tray;
        IProfileCommands&     m_commands;
        ISystemIntegration&   m_system;
        IWindowController&    m_window;
        std::function<void()> m_onQuit;

        std::mutex m_applyMutex; // serialises applyState() so an older state can never overwrite a newer one
        IconKey    m_iconKey;    // guarded by m_applyMutex
        bool       m_started{false};

        std::mutex m_menuMutex;  // guards m_menu (read by the tray thread, written by applyState)
        TrayMenu   m_menu;
    };

} // namespace ui
