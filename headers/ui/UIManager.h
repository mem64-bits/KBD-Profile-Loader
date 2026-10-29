#pragma once

#include "ui/IView.h"
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>

struct GLFWwindow;

namespace UI {

    class UIManager {
    public:
        UIManager();
        ~UIManager();

        UIManager(const UIManager&) = delete;
        UIManager& operator=(const UIManager&) = delete;

        void addView(std::shared_ptr<IView> view);
        void start();
        void stop();

        // Summons or dismisses the quick profile picker window
        void toggleProfilePicker();

        [[nodiscard]] bool isRunning() const noexcept;

    private:
        void renderLoop(std::stop_token stopToken);
        void setupModernSlateStyle();

        std::vector<std::shared_ptr<IView>> m_views;
        std::mutex                          m_viewMutex;
        std::jthread                        m_renderThread;
        std::atomic<bool>                   m_running{false};

        GLFWwindow*                         m_window = nullptr;
    };

} // namespace ui-