#include "ui/imgui/UIManager.h"
#include "files/Logger.h"
#include "ui/PngWriter.h"
#include "ui/ThemeManager.h"
#include "ui/TrayIconRenderer.h"
#include "ui/imgui/ImGuiTheme.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <system_error>
#include <vector>

namespace ui {

namespace {

    constexpr const char* APPLICATION_ID = "kbd-profile-loader";
    constexpr double      IDLE_FRAME_SECONDS = 1.0 / 60.0;
    constexpr double      THEME_POLL_SECONDS = 1.0;

    // The OS's own UI font reads far better than ImGui's built-in monospace one and needs no bundled
    // (and separately licensed) font file. First existing file wins.
    std::vector<std::filesystem::path> systemFontCandidates() {
#if defined(_WIN32)
        const char* windir = std::getenv("WINDIR");
        const std::filesystem::path fonts = std::filesystem::path(windir != nullptr ? windir : "C:\\Windows") / "Fonts";
        return {fonts / "segoeui.ttf"};
#else
        return {
            "/usr/share/fonts/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/TTF/NotoSans-Regular.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/TTF/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        };
#endif
    }

} // namespace

struct UIManager::Impl {
    explicit Impl(UIManagerOptions opts) : options(std::move(opts)) {}

    UIManagerOptions options;

    std::vector<std::shared_ptr<IView>> views;
    std::mutex                          viewsMutex;

    std::atomic<bool> quit{false};
    std::atomic<bool> windowRequested{false}; // what callers want; the UI thread makes it so
    std::atomic<bool> closeQuits{false};
    std::atomic<bool> themeDirty{true};
    std::atomic<bool> glfwReady{false};       // glfwPostEmptyEvent() is only valid while GLFW is initialised

    // Only touched on the UI thread, inside run():
    GLFWwindow*  window{nullptr};
    bool         windowShown{false};
    float        dpiScale{1.0f};
    ImFont*      builtInFont{nullptr};
    ImFont*      customFont{nullptr};
    std::string  loadedFontRequest{"\x01"}; // never equal to a real request, so the first applyFont() always runs

    void wake() const {
        if (glfwReady.load(std::memory_order_acquire)) glfwPostEmptyEvent();
    }

    std::vector<std::shared_ptr<IView>> viewSnapshot() {
        std::lock_guard<std::mutex> lock(viewsMutex);
        return views;
    }

    void applyVisibility(bool visible) {
        windowShown = visible;
        if (visible) {
            glfwShowWindow(window);
        } else {
            glfwHideWindow(window);
        }
        for (const auto& view : viewSnapshot()) view->setVisible(visible);
        LOG_INFO("UI", visible ? "Dashboard shown." : "Dashboard hidden.");
        if (options.onVisibilityChanged) options.onVisibilityChanged(visible);
    }

    void renderFrame(const int frame);
    void applyFont(const std::string& requested);

    static void onGlfwError(const int code, const char* description) {
        // Wayland has no API for some things (window icon, window position): that is expected, not an error.
        if (code == GLFW_FEATURE_UNAVAILABLE) {
            LOG_DEBUG("UI", std::string("GLFW: ") + description);
        } else {
            LOG_WARN("UI", std::string("GLFW error ") + std::to_string(code) + ": " + description);
        }
    }

    static void onWindowClose(GLFWwindow* window) {
        auto* self = static_cast<Impl*>(glfwGetWindowUserPointer(window));
        if (self->closeQuits.load()) {
            self->quit.store(true);
            return;
        }
        glfwSetWindowShouldClose(window, GLFW_FALSE); // the close button hides the window, the tray keeps running
        self->windowRequested.store(false);
    }
};

// Picks the font the theme asks for (theme.json "fontFile"), else the system UI font, else ImGui's own.
void UIManager::Impl::applyFont(const std::string& requested) {
    if (requested == loadedFontRequest) return;
    loadedFontRequest = requested;

    ImGuiIO& io = ImGui::GetIO();
    ImFont* font = nullptr;
    const auto tryLoad = [&](const std::filesystem::path& path) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) return false;
        font = io.Fonts->AddFontFromFileTTF(path.string().c_str());
        if (font != nullptr) LOG_INFO("UI", "Using the font " + path.string());
        return font != nullptr;
    };

    if (!requested.empty()) {
        if (!tryLoad(requested)) LOG_WARN("UI", "Could not load the font '" + requested + "'; using the default font instead.");
    }
    if (font == nullptr && requested.empty()) {
        for (const auto& candidate : systemFontCandidates()) {
            if (tryLoad(candidate)) break;
        }
    }

    io.FontDefault = font != nullptr ? font : builtInFont;
    if (customFont != nullptr && customFont != font) io.Fonts->RemoveFont(customFont);
    customFont = font;
}

void UIManager::Impl::renderFrame(const int frame) {
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    glViewport(0, 0, framebufferWidth, framebufferHeight);

    const Color background = ThemeManager::getInstance().getTheme().palette.background;
    glClearColor(static_cast<float>(background.r) / 255.0f, static_cast<float>(background.g) / 255.0f,
                 static_cast<float>(background.b) / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    for (const auto& view : viewSnapshot()) view->render();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (options.onFrameRendered) options.onFrameRendered(frame);
    glfwSwapBuffers(window);
}

UIManager::UIManager(UIManagerOptions options) : m_impl(std::make_unique<Impl>(std::move(options))) {
    m_impl->windowRequested.store(m_impl->options.startVisible);
}

UIManager::~UIManager() {
    requestQuit();
}

void UIManager::addView(std::shared_ptr<IView> view) {
    if (!view) return;
    std::lock_guard<std::mutex> lock(m_impl->viewsMutex);
    m_impl->views.push_back(std::move(view));
}

void UIManager::requestQuit() noexcept {
    m_impl->quit.store(true);
    m_impl->wake();
}

void UIManager::setCloseButtonQuits(const bool quits) noexcept {
    m_impl->closeQuits.store(quits);
}

void UIManager::showDashboard() {
    m_impl->windowRequested.store(true);
    m_impl->wake();
}

void UIManager::hideDashboard() {
    m_impl->windowRequested.store(false);
    m_impl->wake();
}

void UIManager::toggleDashboard() {
    bool expected = m_impl->windowRequested.load();
    while (!m_impl->windowRequested.compare_exchange_weak(expected, !expected)) {
    }
    m_impl->wake();
}

bool UIManager::isDashboardVisible() const noexcept {
    return m_impl->windowRequested.load();
}

void UIManager::onThemeChanged(const Theme&) {
    m_impl->themeDirty.store(true);
    m_impl->wake();
}

bool UIManager::saveScreenshot(const std::filesystem::path& path) {
    if (m_impl->window == nullptr) return false;

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_impl->window, &width, &height);
    if (width <= 0 || height <= 0) return false;

    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    // OpenGL returns rows bottom-up and with alpha; PNG wants top-down RGB.
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3);
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* source = rgba.data() + static_cast<std::size_t>(height - 1 - y) * static_cast<std::size_t>(width) * 4;
        std::uint8_t* target = rgb.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 3;
        for (int x = 0; x < width; ++x) {
            target[x * 3 + 0] = source[x * 4 + 0];
            target[x * 3 + 1] = source[x * 4 + 1];
            target[x * 3 + 2] = source[x * 4 + 2];
        }
    }
    return writePng(path, width, height, rgb);
}

bool UIManager::run() {
    Impl& s = *m_impl;

    glfwSetErrorCallback(&Impl::onGlfwError);
    if (glfwInit() != GLFW_TRUE) {
        LOG_WARN("UI", "Could not initialise GLFW (no display?). Running without a window.");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHintString(GLFW_WAYLAND_APP_ID, APPLICATION_ID);
    glfwWindowHintString(GLFW_X11_CLASS_NAME, APPLICATION_ID);
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, APPLICATION_ID);

    s.window = glfwCreateWindow(s.options.width, s.options.height, s.options.windowTitle.c_str(), nullptr, nullptr);
    if (s.window == nullptr) {
        LOG_WARN("UI", "Could not create an OpenGL window. Running without a window.");
        glfwTerminate();
        return false;
    }

    glfwSetWindowUserPointer(s.window, &s);
    glfwSetWindowCloseCallback(s.window, &Impl::onWindowClose);
    glfwSetWindowSizeLimits(s.window, 420, 520, GLFW_DONT_CARE, GLFW_DONT_CARE);

    // Window icon: the same procedurally drawn icon as the tray (ignored where the platform has no icons).
    {
        const TrayImage icon = renderTrayIcon(64, ThemeManager::getInstance().getTheme(), 1, true);
        GLFWimage image{icon.width, icon.height, const_cast<unsigned char*>(icon.rgba.data())};
        glfwSetWindowIcon(s.window, 1, &image);
    }

    glfwMakeContextCurrent(s.window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // a tray daemon must not scatter imgui.ini files around
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    s.builtInFont = io.Fonts->AddFontDefaultVector();

    if (!ImGui_ImplGlfw_InitForOpenGL(s.window, true) || !ImGui_ImplOpenGL3_Init("#version 130")) {
        LOG_WARN("UI", "Could not initialise the ImGui backends. Running without a window.");
        ImGui::DestroyContext();
        glfwDestroyWindow(s.window);
        s.window = nullptr;
        glfwTerminate();
        return false;
    }

    ThemeManager::getInstance().addObserver(this);
    s.glfwReady.store(true, std::memory_order_release);
    LOG_INFO("UI", std::string("Window ready (") + (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND ? "Wayland" : glfwGetPlatform() == GLFW_PLATFORM_X11 ? "X11" : "native") + " backend).");

    int    frame = 0;
    double lastThemePoll = glfwGetTime();

    while (!s.quit.load()) {
        const bool wanted = s.windowRequested.load();
        if (wanted != s.windowShown) s.applyVisibility(wanted);

        if (s.windowShown) {
            glfwWaitEventsTimeout(IDLE_FRAME_SECONDS);
        } else {
            glfwWaitEvents(); // hidden: sleep until a tray click, theme change or quit request wakes us
        }
        if (s.quit.load()) break;

        const double now = glfwGetTime();
        if (s.windowShown && now - lastThemePoll >= THEME_POLL_SECONDS) {
            lastThemePoll = now;
            ThemeManager::getInstance().reloadIfChanged(); // hot-reload theme.json edits
        }

        const float scale = ImGui_ImplGlfw_GetContentScaleForWindow(s.window);
        if (scale > 0.0f && scale != s.dpiScale) {
            s.dpiScale = scale;
            s.themeDirty.store(true);
        }
        if (s.themeDirty.exchange(false)) {
            const Theme theme = ThemeManager::getInstance().getTheme();
            s.applyFont(theme.fontFile);
            applyImGuiTheme(theme, s.dpiScale);
        }

        if (!s.windowShown || glfwGetWindowAttrib(s.window, GLFW_ICONIFIED) == GLFW_TRUE) continue;

        s.renderFrame(frame);
        ++frame;
        if (s.options.exitAfterFrames > 0 && frame >= s.options.exitAfterFrames) s.quit.store(true);
    }

    s.glfwReady.store(false, std::memory_order_release);
    ThemeManager::getInstance().removeObserver(this);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(s.window);
    s.window = nullptr;
    glfwTerminate();
    return true;
}

} // namespace ui
