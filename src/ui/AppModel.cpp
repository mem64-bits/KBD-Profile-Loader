#include "ui/AppModel.h"

namespace ui {

AppSnapshot AppModel::snapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

std::uint64_t AppModel::revision() const noexcept {
    return m_revision.load(std::memory_order_acquire);
}

void AppModel::update(const std::function<void(AppSnapshot&)>& mutation) {
    AppSnapshot after;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const AppSnapshot before = m_state;
        mutation(m_state);
        if (m_state == before) return;

        m_revision.fetch_add(1, std::memory_order_acq_rel);
        after = m_state;
    }

    // Notify outside m_mutex so observers may call snapshot() from their callback.
    m_observers.notify([&after](IAppModelObserver& observer) { observer.onAppStateChanged(after); });
}

void AppModel::setStatus(const StatusSeverity severity, std::string text) {
    update([this, severity, &text](AppSnapshot& state) {
        state.status = StatusMessage{severity, std::move(text), ++m_statusSerial};
    });
}

void AppModel::addObserver(IAppModelObserver* observer) {
    m_observers.add(observer);
}

void AppModel::removeObserver(IAppModelObserver* observer) {
    m_observers.remove(observer);
}

} // namespace ui
