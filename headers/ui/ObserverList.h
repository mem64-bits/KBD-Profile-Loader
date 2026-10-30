#pragma once

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <vector>

namespace ui {

    // Thread-safe registry of non-owning observers, shared by every subject in the ui layer
    // (ThemeManager, AppModel, tray icons, ...).
    //
    //  - add()/remove() ignore nullptr, duplicates and unknown observers.
    //  - notify() invokes the callbacks while holding the (recursive) registry lock, so a
    //    remove() issued from another thread blocks until a running callback has finished.
    //    Once remove() returns, the observer will not be called again and may be destroyed.
    //  - A callback may add or remove observers (itself included) on the notifying thread;
    //    observers removed during a notification round are skipped.
    template <typename Observer>
    class ObserverList {
    public:
        ObserverList() = default;
        ~ObserverList() = default;

        ObserverList(const ObserverList&) = delete;
        ObserverList& operator=(const ObserverList&) = delete;
        ObserverList(ObserverList&&) = delete;
        ObserverList& operator=(ObserverList&&) = delete;

        bool add(Observer* observer) {
            if (observer == nullptr) return false;

            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            if (contains(observer)) return false;
            m_observers.push_back(observer);
            return true;
        }

        bool remove(Observer* observer) {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            const auto it = std::find(m_observers.begin(), m_observers.end(), observer);
            if (it == m_observers.end()) return false;
            m_observers.erase(it);
            return true;
        }

        template <typename Callback>
        void notify(Callback&& callback) const {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            const std::vector<Observer*> snapshot = m_observers;
            for (Observer* observer : snapshot) {
                if (contains(observer)) callback(*observer);
            }
        }

        [[nodiscard]] std::size_t size() const {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            return m_observers.size();
        }

    private:
        // Caller must hold m_mutex.
        [[nodiscard]] bool contains(const Observer* observer) const {
            return std::find(m_observers.begin(), m_observers.end(), observer) != m_observers.end();
        }

        mutable std::recursive_mutex m_mutex;
        std::vector<Observer*>       m_observers;
    };

} // namespace ui
