#include "ui/UIManager.h"
#include "files/Logger.h"
#include "ui/QuickProfilePickerView.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <iostream>

namespace UI {

UIManager::UIManager() = default;

UIManager::~UIManager() {
    stop();
}

void UIManager::addView(std::shared_ptr<IView> view) {
    std::lock_guard<std::mutex> lock(m_viewMutex);
    if (view) {
        m_views.push_back(std::move(view));
    }
}

void UIManager::start() {
    if (m_running.load(std::memory_order_acquire)) return;
    m_running.store(true, std::memory_order_release);

    m_renderThread = std::jthread([this](std::stop_token stopToken) {
        renderLoop(stopToken);
    });

    LOG_INFO("UI", "UIManager background render thread started.");
}

void UIManager::stop() {
    if (!m_running.exchange(false, std::memory_order_acq_rel)) return;

    if (m_window) {
        glfwSetWindowShouldClose(m_window, GLFW_TRUE);
    }

    if (m_renderThread.joinable()) {
        m_renderThread.request_stop();
        m_renderThread.join();
    }

    LOG_INFO("UI", "UIManager render thread stopped.");
}

void UIManager::toggleProfilePicker() {
    std::lock_guard<std::mutex> lock(m_viewMutex);
    for (auto& view : m_views) {
        // If it's a QuickProfilePickerView, toggle its visibility
        if (auto* picker = dynamic_cast<class QuickProfilePickerView*>(view.get())) {
            bool current = picker->isVisible();
            picker->setVisible(!current);
            LOG_INFO("UI", std::string("Quick Profile Switcher window toggled: ") + (!current ? "OPEN" : "CLOSED"));
        }
    }
}

bool UIManager::isRunning() const noexcept {
    return m_running.load(std::memory_order_acquire);
}

void UIManager::renderLoop(std::stop_token stopToken) {
    if (!glfwInit()) {
        LOG_ERROR("UI", "Failed to initialize GLFW.");
        return;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Create a hidden window context that manages ImGui rendering when summoned
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    m_window = glfwCreateWindow(640, 480, "Keychron Dashboard Context", nullptr, nullptr);
    if (!m_window) {
        LOG_ERROR("UI", "Failed to create GLFW window context.");
        glfwTerminate();
        return;
    }

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1); // VSync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    setupModernSlateStyle();

    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    LOG_INFO("UI", "ImGui & OpenGL3 context successfully initialized.");

    while (!stopToken.stop_requested() && !glfwWindowShouldClose(m_window)) {
        glfwPollEvents();

        // Check if any view is currently visible. If so, make window visible.
        bool anyVisible = false;
        {
            std::lock_guard<std::mutex> lock(m_viewMutex);
            for (const auto& view : m_views) {
                if (view->isVisible()) {
                    anyVisible = true;
                    break;
                }
            }
        }

        if (anyVisible) {
            glfwShowWindow(m_window);
        } else {
            glfwHideWindow(m_window);
            std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Sleep to save CPU when UI is closed
            continue;
        }

        int displayW, displayH;
        glfwGetFramebufferSize(m_window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Render all registered views
        {
            std::lock_guard<std::mutex> lock(m_viewMutex);
            for (auto& view : m_views) {
                view->render();
            }
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(m_window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    glfwTerminate();
}

void UIManager::setupModernSlateStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 8.0f;
    style.FrameRounding     = 5.0f;
    style.PopupRounding     = 5.0f;
    style.WindowBorderSize  = 1.0f;
    style.WindowPadding     = ImVec2(14.0f, 12.0f);
    style.ItemSpacing       = ImVec2(10.0f, 8.0f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]       = ImVec4(0.09f, 0.11f, 0.14f, 0.96f);// Slate 900
    colors[ImGuiCol_Border]         = ImVec4(0.23f, 0.51f, 0.96f, 0.65f);// Cobalt highlight
    colors[ImGuiCol_Text]           = ImVec4(0.94f, 0.96f, 0.98f, 1.00f);
    colors[ImGuiCol_TextDisabled]   = ImVec4(0.55f, 0.60f, 0.68f, 1.00f);
    colors[ImGuiCol_FrameBg]        = ImVec4(0.15f, 0.18f, 0.23f, 0.85f);
    colors[ImGuiCol_Button]         = ImVec4(0.23f, 0.51f, 0.96f, 0.80f);
    colors[ImGuiCol_ButtonHovered]  = ImVec4(0.30f, 0.58f, 1.00f, 1.00f);
    colors[ImGuiCol_ButtonActive]   = ImVec4(0.18f, 0.40f, 0.78f, 1.00f);
}

} // namespace ui