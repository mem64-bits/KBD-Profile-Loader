#include "ui/imgui/UIManager.h"
#include "files/Logger.h"
#include "ui/PngWriter.h"
#include "ui/ThemeManager.h"
#include "ui/TrayIconRenderer.h"
#include "ui/imgui/ControllerArt.h"
#include "ui/imgui/Glyphs.h"
#include "ui/imgui/ImGuiTheme.h"
#include "ui/imgui/SdlGamepadSource.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <implot.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <system_error>
#include <vector>

namespace ui {

namespace {

    constexpr const char* APPLICATION_ID     = "kbd-profile-loader";
    constexpr const char* APPLICATION_NAME   = "Keychron K2 HE Profile Loader";
    constexpr int         IDLE_FRAME_MS      = 16; // ~60 fps while the window is shown
    constexpr Uint64      THEME_POLL_NS      = 1'000'000'000ULL;
    constexpr int         MIN_WIDTH          = 920; // sidebar + a keyboard view that is still readable
    constexpr int         MIN_HEIGHT         = 660;

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

    float contentScaleOf(SDL_Window* window) {
        const SDL_DisplayID display = window != nullptr ? SDL_GetDisplayForWindow(window) : SDL_GetPrimaryDisplay();
        const float scale = SDL_GetDisplayContentScale(display);
        return scale > 0.0f ? scale : 1.0f;
    }

} // namespace

struct UIManager::Impl {
    explicit Impl(UIManagerOptions opts) : options(std::move(opts)), gamepads(std::make_shared<SdlGamepadSource>()) {}

    UIManagerOptions options;
    std::shared_ptr<SdlGamepadSource> gamepads;

    std::vector<std::shared_ptr<IView>> views;
    std::mutex                          viewsMutex;

    std::atomic<bool>   quit{false};
    std::atomic<bool>   windowRequested{false}; // what callers want; the UI thread makes it so
    std::atomic<bool>   closeQuits{false};
    std::atomic<bool>   themeDirty{true};
    std::atomic<bool>   sdlReady{false};        // SDL_PushEvent is only valid while SDL is initialised
    std::atomic<Uint32> wakeEvent{0};

    // Only touched on the UI thread, inside run():
    SDL_Window*   window{nullptr};
    SDL_GLContext glContext{nullptr};
    bool          windowShown{false};
    float         dpiScale{1.0f};
    ImFont*       builtInFont{nullptr};
    ImFont*       customFont{nullptr};
    std::string   loadedFontRequest{"\x01"}; // never equal to a real request, so the first applyFont() always runs

    // Any thread: makes SDL_WaitEvent return so the loop looks at the flags again.
    void wake() const {
        if (!sdlReady.load(std::memory_order_acquire)) return;
        SDL_Event event{};
        event.type = wakeEvent.load(std::memory_order_acquire);
        if (event.type != 0) SDL_PushEvent(&event);
    }

    std::vector<std::shared_ptr<IView>> viewSnapshot() {
        std::lock_guard<std::mutex> lock(viewsMutex);
        return views;
    }

    void applyVisibility(const bool visible) {
        windowShown = visible;
        if (visible) {
            SDL_ShowWindow(window);
            SDL_RaiseWindow(window);
        } else {
            SDL_HideWindow(window);
        }
        gamepads->setActive(visible);
        for (const auto& view : viewSnapshot()) view->setVisible(visible);
        LOG_INFO("UI", visible ? "Dashboard shown." : "Dashboard hidden.");
        if (options.onVisibilityChanged) options.onVisibilityChanged(visible);
    }

    void handleEvent(const SDL_Event& event) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        gamepads->handleEvent(event);

        switch (event.type) {
            case SDL_EVENT_QUIT:
                quit.store(true); // the OS is ending the session (signals are handled by main)
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (event.window.windowID != SDL_GetWindowID(window)) break;
                if (closeQuits.load()) {
                    quit.store(true);
                } else {
                    windowRequested.store(false); // the close button hides the window, the tray keeps running
                }
                break;
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
                themeDirty.store(true);
                break;
            default:
                break;
        }
    }

    void renderFrame(int frame);
    void applyFont(const std::string& requested);
    void teardown(bool imguiReady);
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
    SDL_GetWindowSizeInPixels(window, &framebufferWidth, &framebufferHeight);
    glViewport(0, 0, framebufferWidth, framebufferHeight);

    const Color background = ThemeManager::getInstance().getTheme().palette.background;
    glClearColor(static_cast<float>(background.r) / 255.0f, static_cast<float>(background.g) / 255.0f,
                 static_cast<float>(background.b) / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    for (const auto& view : viewSnapshot()) view->render();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (options.onFrameRendered) options.onFrameRendered(frame);
    SDL_GL_SwapWindow(window);
}

void UIManager::Impl::teardown(const bool imguiReady) {
    sdlReady.store(false, std::memory_order_release);
    gamepads->detach();
    if (imguiReady) {
        art::XboxControllerArt::release(); // GPU textures: before the GL context goes
        art::DualSenseArt::release();
        glyphs::release();
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
    }
    if (ImPlot::GetCurrentContext() != nullptr) ImPlot::DestroyContext();
    if (ImGui::GetCurrentContext() != nullptr) ImGui::DestroyContext();
    if (glContext != nullptr) SDL_GL_DestroyContext(glContext);
    glContext = nullptr;
    if (window != nullptr) SDL_DestroyWindow(window);
    window = nullptr;
    SDL_Quit();
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

std::shared_ptr<gamepad::IGamepadSource> UIManager::gamepadSource() const {
    return m_impl->gamepads;
}

bool UIManager::saveScreenshot(const std::filesystem::path& path) {
    if (m_impl->window == nullptr) return false;

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(m_impl->window, &width, &height);
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

    // main() owns Ctrl+C / SIGTERM and the shutdown order; SDL must not turn them into its own quit events.
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    // Closing the (only) window hides it; the tray keeps the program alive.
    SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
    // The controller view must keep updating while a game or an emulator has the focus.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_APP_ID, APPLICATION_ID); // Wayland app id / X11 class: the desktop groups the window with its icon
    SDL_SetAppMetadata(APPLICATION_NAME, KBD_VERSION, APPLICATION_ID);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_WARN("UI", std::string("Could not initialise SDL video (no display?): ") + SDL_GetError() + ". Running without a window.");
        SDL_Quit();
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

    // Logical size times the display's scale, so the window has the same proportions on a 150 % display.
    const float startScale = contentScaleOf(nullptr);
    const SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    s.window = SDL_CreateWindow(s.options.windowTitle.c_str(), static_cast<int>(static_cast<float>(s.options.width) * startScale),
                                static_cast<int>(static_cast<float>(s.options.height) * startScale), flags);
    if (s.window == nullptr) {
        LOG_WARN("UI", std::string("Could not create an OpenGL window: ") + SDL_GetError() + ". Running without a window.");
        s.teardown(false);
        return false;
    }
    SDL_SetWindowMinimumSize(s.window, static_cast<int>(MIN_WIDTH * startScale), static_cast<int>(MIN_HEIGHT * startScale));
    // Previews (screenshots, smoke tests) go to the corner, away from wherever the pointer rests.
    if (s.options.exitAfterFrames > 0) {
        SDL_SetWindowPosition(s.window, 0, 0);
    } else {
        SDL_SetWindowPosition(s.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }

    // Window icon: the same procedurally drawn icon as the tray (ignored where the platform has no icons).
    {
        TrayImage icon = renderTrayIcon(64, ThemeManager::getInstance().getTheme(), 1, true);
        if (SDL_Surface* surface = SDL_CreateSurfaceFrom(icon.width, icon.height, SDL_PIXELFORMAT_RGBA32, icon.rgba.data(), icon.width * 4)) {
            SDL_SetWindowIcon(s.window, surface); // SDL keeps its own copy
            SDL_DestroySurface(surface);
        }
    }

    s.glContext = SDL_GL_CreateContext(s.window);
    if (s.glContext == nullptr) {
        LOG_WARN("UI", std::string("Could not create an OpenGL 3.0 context: ") + SDL_GetError() + ". Running without a window.");
        s.teardown(false);
        return false;
    }
    SDL_GL_MakeCurrent(s.window, s.glContext);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // a tray daemon must not scatter imgui.ini files around
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // not the gamepad: the keyboard IS the gamepad here
    s.builtInFont = io.Fonts->AddFontDefaultVector();

    if (!ImGui_ImplSDL3_InitForOpenGL(s.window, s.glContext) || !ImGui_ImplOpenGL3_Init("#version 130")) {
        LOG_WARN("UI", "Could not initialise the ImGui backends. Running without a window.");
        s.teardown(false);
        return false;
    }
    // ImGui must not open controllers of its own (that is SdlGamepadSource's job).
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);

    ThemeManager::getInstance().addObserver(this);
    s.wakeEvent.store(SDL_RegisterEvents(1), std::memory_order_release);
    s.sdlReady.store(true, std::memory_order_release);
    s.gamepads->attach();
    const char* driver = SDL_GetCurrentVideoDriver();
    LOG_INFO("UI", std::string("Window ready (SDL ") + std::to_string(SDL_VERSIONNUM_MAJOR(SDL_GetVersion())) + "." +
                       std::to_string(SDL_VERSIONNUM_MINOR(SDL_GetVersion())) + "." + std::to_string(SDL_VERSIONNUM_MICRO(SDL_GetVersion())) +
                       ", " + (driver != nullptr ? driver : "unknown") + " video driver).");

    int    frame = 0;
    Uint64 lastThemePoll = SDL_GetTicksNS();

    while (!s.quit.load()) {
        const bool wanted = s.windowRequested.load();
        if (wanted != s.windowShown) s.applyVisibility(wanted);

        SDL_Event event;
        // Shown: wake up at least once a frame (animations, live data). Hidden: sleep until a tray click,
        // a theme change or a quit request pushes an event.
        const bool gotEvent = s.windowShown ? SDL_WaitEventTimeout(&event, IDLE_FRAME_MS) : SDL_WaitEvent(&event);
        if (gotEvent) {
            s.handleEvent(event);
            while (SDL_PollEvent(&event)) s.handleEvent(event);
        }
        if (s.quit.load()) break;

        const Uint64 now = SDL_GetTicksNS();
        if (s.windowShown && now - lastThemePoll >= THEME_POLL_NS) {
            lastThemePoll = now;
            ThemeManager::getInstance().reloadIfChanged(); // hot-reload theme.json edits
        }

        const float scale = contentScaleOf(s.window);
        if (scale != s.dpiScale) {
            s.dpiScale = scale;
            s.themeDirty.store(true);
        }
        if (s.themeDirty.exchange(false)) {
            const Theme theme = ThemeManager::getInstance().getTheme();
            s.applyFont(theme.fontFile);
            applyImGuiTheme(theme, s.dpiScale);
        }

        if (!s.windowShown || (SDL_GetWindowFlags(s.window) & SDL_WINDOW_MINIMIZED) != 0) continue;

        s.renderFrame(frame);
        ++frame;
        if (s.options.exitAfterFrames > 0 && frame >= s.options.exitAfterFrames) s.quit.store(true);
    }

    ThemeManager::getInstance().removeObserver(this);
    s.teardown(true);
    return true;
}

} // namespace ui
