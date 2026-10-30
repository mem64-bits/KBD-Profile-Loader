#pragma once

#include "ui/AppState.h"
#include "ui/ObserverList.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace ui {

    class IAppModelObserver {
    public:
        virtual ~IAppModelObserver() = default;

        // Called on whichever thread changed the model, only when the state really changed.
        // The snapshot is a hint: a later change may already be on its way.
        virtual void onAppStateChanged(const AppSnapshot& snapshot) = 0;
    };

    // Thread-safe holder of the AppSnapshot (the "subject" of the Observer pattern). Writers call
    // update(); readers either poll snapshot() every frame or subscribe as an IAppModelObserver.
    class AppModel {
    public:
        AppModel() = default;
        ~AppModel() = default;

        AppModel(const AppModel&) = delete;
        AppModel& operator=(const AppModel&) = delete;
        AppModel(AppModel&&) = delete;
        AppModel& operator=(AppModel&&) = delete;

        [[nodiscard]] AppSnapshot snapshot() const;

        // Incremented on every change; cheap "has anything changed?" check for render loops.
        [[nodiscard]] std::uint64_t revision() const noexcept;

        // Runs `mutation` on the state under the model's lock (it must not call back into the model),
        // then notifies observers if the state differs from before.
        void update(const std::function<void(AppSnapshot&)>& mutation);

        // Shorthand that replaces the status line and gives it a fresh serial number.
        void setStatus(StatusSeverity severity, std::string text);

        void addObserver(IAppModelObserver* observer);
        void removeObserver(IAppModelObserver* observer);

    private:
        mutable std::mutex              m_mutex;
        AppSnapshot                     m_state;
        std::uint64_t                   m_statusSerial{0}; // guarded by m_mutex
        std::atomic<std::uint64_t>      m_revision{0};
        ObserverList<IAppModelObserver> m_observers;
    };

} // namespace ui
