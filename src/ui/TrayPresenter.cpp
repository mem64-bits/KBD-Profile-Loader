#include "ui/TrayPresenter.h"
#include "ui/ThemeManager.h"
#include "ui/TrayIconRenderer.h"
#include "ui/TrayMenuBuilder.h"

#include <utility>

namespace ui {

TrayPresenter::TrayPresenter(AppModel& model, ITrayIcon& tray, IProfileCommands& commands, ISystemIntegration& system,
                             IWindowController& window, std::function<void()> onQuit)
    : m_model(model),
      m_tray(tray),
      m_commands(commands),
      m_system(system),
      m_window(window),
      m_onQuit(std::move(onQuit))
{}

TrayPresenter::~TrayPresenter() {
    stop();
}

bool TrayPresenter::start() {
    if (m_started) return m_tray.isRunning();
    m_started = true;

    m_tray.addObserver(this);
    m_model.addObserver(this);
    ThemeManager::getInstance().addObserver(this);

    applyState(m_model.snapshot()); // the first thing the user sees is already complete
    return m_tray.start();
}

void TrayPresenter::stop() {
    if (!m_started) return;
    m_started = false;

    // Unsubscribe first: once these return, no callback is running or will run again.
    ThemeManager::getInstance().removeObserver(this);
    m_model.removeObserver(this);
    m_tray.removeObserver(this);
    m_tray.stop();
}

void TrayPresenter::applyState(const AppSnapshot& state) {
    std::lock_guard<std::mutex> applying(m_applyMutex);

    TrayMenu menu = tray::buildMenu(state, m_system.isAutostartEnabled());
    {
        std::lock_guard<std::mutex> lock(m_menuMutex);
        m_menu = menu;
    }
    m_tray.setMenu(std::move(menu));
    m_tray.setTooltip(tray::tooltipTitle(), tray::tooltipDescription(state));

    // Re-rendering the icon is the expensive part, so only do it when its inputs changed.
    ThemeManager& themes = ThemeManager::getInstance();
    const IconKey key{state.activeSlot, state.device.connected, themes.revision()};
    if (!(key == m_iconKey)) {
        m_iconKey = key;
        m_tray.setIcons(renderTrayIcons(themes.getTheme(), state.activeSlot, state.device.connected));
    }
}

void TrayPresenter::onAppStateChanged(const AppSnapshot& snapshot) {
    applyState(snapshot);
}

void TrayPresenter::onThemeChanged(const Theme&) {
    applyState(m_model.snapshot());
}

void TrayPresenter::onTrayActivated() {
    m_window.toggleDashboard();
}

void TrayPresenter::onTraySecondaryActivated() {
    m_commands.cycleProfile(true);
}

void TrayPresenter::onTrayMenuItemSelected(const int itemId) {
    TrayMenuItem item;
    {
        std::lock_guard<std::mutex> lock(m_menuMutex);
        const TrayMenuItem* found = m_menu.find(itemId);
        if (found == nullptr) return; // stale id from a menu the host cached before our last update
        item = *found;
    }

    if (!item.enabled) return;
    dispatch(item.action);
}

void TrayPresenter::dispatch(const TrayAction action) {
    switch (action) {
        case TrayAction::OpenDashboard:      m_window.showDashboard(); break;
        case TrayAction::ActivateSlot1:      m_commands.activateSlot(0); break;
        case TrayAction::ActivateSlot2:      m_commands.activateSlot(1); break;
        case TrayAction::ActivateSlot3:      m_commands.activateSlot(2); break;
        case TrayAction::NextProfile:        m_commands.cycleProfile(true); break;
        case TrayAction::PreviousProfile:    m_commands.cycleProfile(false); break;
        case TrayAction::OpenProfilesFolder: m_system.openProfilesFolder(); break;
        case TrayAction::OpenConfigFile:     m_system.openConfigFile(); break;
        case TrayAction::OpenLogFile:        m_system.openLogFile(); break;
        case TrayAction::ToggleAutostart:
            m_system.setAutostartEnabled(!m_system.isAutostartEnabled());
            applyState(m_model.snapshot()); // the check mark lives only in the menu, not in the model
            break;
        case TrayAction::Quit:
            if (m_onQuit) m_onQuit();
            break;
        case TrayAction::None:
            break;
    }
}

} // namespace ui
