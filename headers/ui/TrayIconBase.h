#pragma once

#include "ui/ITrayIcon.h"
#include "ui/ObserverList.h"

#include <mutex>
#include <string>
#include <vector>

namespace ui {

    // Shared plumbing for tray backends: caches the latest icon / tooltip / menu (thread-safe, with
    // change detection) and fans tray events out to the registered ITrayObservers. A backend only has to
    // draw the cached state when onStateChanged() fires and call notify*() when the user interacts.
    class TrayIconBase : public ITrayIcon {
    public:
        ~TrayIconBase() override;

        TrayIconBase(const TrayIconBase&) = delete;
        TrayIconBase& operator=(const TrayIconBase&) = delete;
        TrayIconBase(TrayIconBase&&) = delete;
        TrayIconBase& operator=(TrayIconBase&&) = delete;

        void setIcons(std::vector<TrayImage> images) final;
        void setTooltip(std::string title, std::string description) final;
        void setMenu(TrayMenu menu) final;

        void addObserver(ITrayObserver* observer) final;
        void removeObserver(ITrayObserver* observer) final;

    protected:
        TrayIconBase() = default;

        struct State {
            std::vector<TrayImage> icons;
            std::string            title;
            std::string            description;
            TrayMenu               menu;
        };

        // Bit flags describing what changed.
        enum Change : unsigned {
            IconChanged    = 1u << 0,
            TooltipChanged = 1u << 1,
            MenuChanged    = 1u << 2,
        };

        // Called on the thread that changed the state, only when something actually changed.
        virtual void onStateChanged(unsigned changes) = 0;

        [[nodiscard]] State state() const;

        void notifyActivated();
        void notifySecondaryActivated();
        void notifyMenuItemSelected(int itemId);

    private:
        mutable std::mutex           m_stateMutex;
        State                        m_state;
        ObserverList<ITrayObserver>  m_observers;
    };

} // namespace ui
