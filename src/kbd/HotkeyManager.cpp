#if defined(_WIN32)

#include "kbd/HotkeyManager.h"
#include <iostream>
#include <utility>

namespace kbd {

constexpr int HOTKEY_ID_PREV = 1001;
constexpr int HOTKEY_ID_NEXT = 1002;

static void logRegistrationError(const std::string& name, DWORD err) {
    std::cerr << "  [!] Failed to register " << name << " (Error " << err << "): ";
    if (err == 1422) { // ERROR_HOTKEY_ALREADY_REGISTERED
        std::cerr << "Hotkey is ALREADY IN USE by another program (e.g. Discord, OBS, GeForce Experience).\n"
                  << "      Please edit config.ini to choose a different combination.\n";
    } else if (err == 87) { // ERROR_INVALID_PARAMETER
        std::cerr << "Invalid keycode or modifier parameter.\n";
    } else {
        std::cerr << "Win32 RegisterHotKey returned failure code.\n";
    }
}

HotkeyManager::HotkeyManager(ActionCallback onNext, ActionCallback onPrev,
                             UINT nextModifiers, UINT nextVk,
                             UINT prevModifiers, UINT prevVk)
    : m_onNext(std::move(onNext)),
      m_onPrev(std::move(onPrev)),
      m_nextMod(nextModifiers),
      m_nextVk(nextVk),
      m_prevMod(prevModifiers),
      m_prevVk(prevVk)
{}

HotkeyManager::~HotkeyManager() {
    stop();
}

void HotkeyManager::start() {
    if (m_running.load(std::memory_order_acquire)) return;
    m_running.store(true, std::memory_order_release);

    m_thread = std::jthread([this](std::stop_token stopToken) {
        threadWorker(std::move(stopToken));
    });
}

void HotkeyManager::stop() {
    if (!m_running.exchange(false, std::memory_order_acq_rel)) return;

    if (m_threadId != 0) {
        PostThreadMessageW(m_threadId, WM_QUIT, 0, 0);
    }

    if (m_thread.joinable()) {
        m_thread.request_stop();
        m_thread.join();
        std::cout << "[HotkeyManager] Background hotkey thread stopped.\n";
    }
}

bool HotkeyManager::isRunning() const noexcept {
    return m_running.load(std::memory_order_acquire);
}

void HotkeyManager::threadWorker(const std::stop_token& stopToken) {
    m_threadId = GetCurrentThreadId();

    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    bool prevOk = (m_prevVk != 0) && RegisterHotKey(nullptr, HOTKEY_ID_PREV, m_prevMod, m_prevVk);
    if (!prevOk && m_prevVk != 0) {
        logRegistrationError("Prev Hotkey", GetLastError());
    }

    bool nextOk = (m_nextVk != 0) && RegisterHotKey(nullptr, HOTKEY_ID_NEXT, m_nextMod, m_nextVk);
    if (!nextOk && m_nextVk != 0) {
        logRegistrationError("Next Hotkey", GetLastError());
    }

    while (!stopToken.stop_requested() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_HOTKEY) {
            if (msg.wParam == HOTKEY_ID_PREV && m_onPrev) {
                m_onPrev();
            } else if (msg.wParam == HOTKEY_ID_NEXT && m_onNext) {
                m_onNext();
            }
        }
    }

    if (prevOk) UnregisterHotKey(nullptr, HOTKEY_ID_PREV);
    if (nextOk) UnregisterHotKey(nullptr, HOTKEY_ID_NEXT);
    m_threadId = 0;
}

} // namespace kbd

#endif // _WIN32