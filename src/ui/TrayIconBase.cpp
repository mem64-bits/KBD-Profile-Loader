#include "ui/TrayIconBase.h"

#include <utility>

namespace ui {

TrayIconBase::~TrayIconBase() = default;

TrayIconBase::State TrayIconBase::state() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_state;
}

void TrayIconBase::setIcons(std::vector<TrayImage> images) {
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (m_state.icons == images) return;
        m_state.icons = std::move(images);
    }
    onStateChanged(IconChanged);
}

void TrayIconBase::setTooltip(std::string title, std::string description) {
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (m_state.title == title && m_state.description == description) return;
        m_state.title       = std::move(title);
        m_state.description = std::move(description);
    }
    onStateChanged(TooltipChanged);
}

void TrayIconBase::setMenu(TrayMenu menu) {
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (m_state.menu == menu) return;
        m_state.menu = std::move(menu);
    }
    onStateChanged(MenuChanged);
}

void TrayIconBase::addObserver(ITrayObserver* observer) {
    m_observers.add(observer);
}

void TrayIconBase::removeObserver(ITrayObserver* observer) {
    m_observers.remove(observer);
}

void TrayIconBase::notifyActivated() {
    m_observers.notify([](ITrayObserver& observer) { observer.onTrayActivated(); });
}

void TrayIconBase::notifySecondaryActivated() {
    m_observers.notify([](ITrayObserver& observer) { observer.onTraySecondaryActivated(); });
}

void TrayIconBase::notifyMenuItemSelected(const int itemId) {
    m_observers.notify([itemId](ITrayObserver& observer) { observer.onTrayMenuItemSelected(itemId); });
}

} // namespace ui
