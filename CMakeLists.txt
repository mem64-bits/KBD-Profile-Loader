cmake_minimum_required(VERSION 3.25)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(KBD_Profile_Loader VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force all binaries into the root build directory regardless of generator (Ninja vs Visual Studio)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}")

# Force all external submodules (especially hidapi) to build statically into the .exe
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)

# Linux system tray: a D-Bus StatusNotifierItem (works on Wayland and X11). It needs GIO, which every
# desktop Linux already ships; without it the daemon still builds and simply runs without a tray icon.
option(KBD_ENABLE_SNI_TRAY "Build the Linux D-Bus system tray icon when GIO is available" ON)
if(UNIX AND NOT APPLE AND KBD_ENABLE_SNI_TRAY)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(KBD_GIO IMPORTED_TARGET gio-2.0)
    endif()
    if(NOT TARGET PkgConfig::KBD_GIO)
        message(WARNING "GIO (libglib2.0-dev / glib2) not found: building WITHOUT the Linux system tray. "
                "Install it and reconfigure to get the tray icon.")
    endif()
endif()

# Settings shared by the daemon and the unit tests.
add_library(kbd_build_options INTERFACE)
target_compile_options(kbd_build_options INTERFACE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>
)
target_compile_definitions(kbd_build_options INTERFACE
        KBD_VERSION="${PROJECT_VERSION}"
        $<$<BOOL:${WIN32}>:NOMINMAX>
        $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
)
if(TARGET PkgConfig::KBD_GIO)
    target_compile_definitions(kbd_build_options INTERFACE KBD_HAVE_SNI_TRAY=1)
    target_link_libraries(kbd_build_options INTERFACE PkgConfig::KBD_GIO)
endif()
if(WIN32)
    # tray icon (shell32), Startup shortcut (ole32/uuid), message window (user32)
    target_link_libraries(kbd_build_options INTERFACE shell32 ole32 uuid user32)
endif()

# ----------------------------------------------------
# 1. Add Local Git Submodules (external/*)
# ----------------------------------------------------

# HIDAPI
set(HIDAPI_WITH_LIBUSB OFF CACHE BOOL "" FORCE)
add_subdirectory(external/hidapi)

# nlohmann_json (Header-only target)
add_subdirectory(external/json)

# SDL3: the window, the OpenGL context, keyboard / mouse input and game controllers (it replaced GLFW).
# Fetched as a pinned, hash-checked release and linked statically, like the submodules above. To build
# offline, point CMake at an unpacked copy: -DFETCHCONTENT_SOURCE_DIR_SDL3=C:/path/to/SDL3-3.4.20
include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
# Subsystems this app does not use (it has its own tray icon and draws with OpenGL directly).
foreach(KBD_SDL_UNUSED AUDIO CAMERA DIALOG GPU RENDER TRAY VULKAN)
    set(SDL_${KBD_SDL_UNUSED} OFF CACHE BOOL "" FORCE)
endforeach()
# Optional X11 extensions SDL would otherwise insist on (often not installed as -dev packages; unused here).
set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)
set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
        SDL3
        URL      https://github.com/libsdl-org/SDL/releases/download/release-3.4.20/SDL3-3.4.20.tar.gz
        URL_HASH SHA256=f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(SDL3)

# ImPlot: plots for the actuation, curve and SOCD diagrams (MIT, the standard plotting add-on for Dear ImGui).
# Pinned to the v1.1 release commit. Offline: -DFETCHCONTENT_SOURCE_DIR_IMPLOT=C:/path/to/implot
FetchContent_Declare(
        implot
        GIT_REPOSITORY https://github.com/epezent/implot.git
        GIT_TAG        819f200639455a58b6b3d4156b70e4271aeb2d00 # v1.1
)
FetchContent_MakeAvailable(implot) # ImPlot has no CMakeLists.txt, so this only downloads it

# Images compiled into the executable (the controller artwork), so the app needs no files next to it.
# Each file becomes a byte array in generated/EmbeddedAssets.cpp; editing an image re-runs CMake.
set(KBD_EMBEDDED_ASSETS
        assets/controller/xbox-series-atlas.png
        assets/controller/dualsense-atlas.png
        assets/glyphs/kenney-input-prompts.png
        assets/glyphs/zacksly-prompts.png
)
set(KBD_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${KBD_GENERATED_DIR})
set(KBD_EMBED_SOURCE "// Generated by CMakeLists.txt from assets/: do not edit.\n#include \"ui/EmbeddedAssets.h\"\n\nnamespace ui::assets {\n")
foreach(KBD_ASSET ${KBD_EMBEDDED_ASSETS})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/${KBD_ASSET})
    file(READ ${CMAKE_SOURCE_DIR}/${KBD_ASSET} KBD_HEX HEX)
    # 32 bytes per line (some compilers dislike one multi-megabyte line), then "0x.." for every byte.
    string(REPEAT "[0-9a-f]" 64 KBD_LINE)
    string(REGEX REPLACE "(${KBD_LINE})" "\\1\n" KBD_HEX "${KBD_HEX}")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," KBD_HEX "${KBD_HEX}")
    string(MAKE_C_IDENTIFIER "${KBD_ASSET}" KBD_ASSET_ID)
    string(APPEND KBD_EMBED_SOURCE "    static const unsigned char ${KBD_ASSET_ID}[] = {${KBD_HEX}};\n")
endforeach()
string(APPEND KBD_EMBED_SOURCE "\n    std::span<const unsigned char> find(std::string_view path) noexcept {\n")
foreach(KBD_ASSET ${KBD_EMBEDDED_ASSETS})
    string(MAKE_C_IDENTIFIER "${KBD_ASSET}" KBD_ASSET_ID)
    string(APPEND KBD_EMBED_SOURCE "        if (path == \"${KBD_ASSET}\") return {${KBD_ASSET_ID}, sizeof(${KBD_ASSET_ID})};\n")
endforeach()
string(APPEND KBD_EMBED_SOURCE "        return {};\n    }\n\n} // namespace ui::assets\n")
file(CONFIGURE OUTPUT ${KBD_GENERATED_DIR}/EmbeddedAssets.cpp CONTENT "${KBD_EMBED_SOURCE}")

# ----------------------------------------------------
# 2. Compile Dear ImGui + Backends Target
# ----------------------------------------------------
add_library(imgui_backend STATIC
        external/imgui/imgui.cpp
        external/imgui/imgui_draw.cpp
        external/imgui/imgui_tables.cpp
        external/imgui/imgui_widgets.cpp
        external/imgui/backends/imgui_impl_sdl3.cpp
        external/imgui/backends/imgui_impl_opengl3.cpp
        ${implot_SOURCE_DIR}/implot.cpp
        ${implot_SOURCE_DIR}/implot_items.cpp
        ${KBD_GENERATED_DIR}/EmbeddedAssets.cpp
)

target_include_directories(imgui_backend PUBLIC
        external/imgui
        external/imgui/backends
        ${implot_SOURCE_DIR}
        headers
)

target_link_libraries(imgui_backend PUBLIC SDL3::SDL3 OpenGL::GL)

# ----------------------------------------------------
# 3. Main Daemon Executable Target
# ----------------------------------------------------
add_executable(KBD_Profile_Loader
        # --- src/main ---
        src/main/main.cpp

        # --- src/kbd ---
        src/kbd/DeviceWatcher.cpp
        src/kbd/FirmwareModel.cpp
        src/kbd/GameController.cpp
        src/kbd/HotkeyManager.cpp
        src/kbd/IpcServer.cpp
        src/kbd/KbdException.cpp
        src/kbd/KeyboardDevice.cpp
        src/kbd/KeyboardProfile.cpp
        src/kbd/KeychronProtocol.cpp
        src/kbd/ProfileMemory.cpp

        # --- src/files ---
        src/files/AppPaths.cpp
        src/files/DirectoryWatcher.cpp
        src/files/IniConfig.cpp
        src/files/Logger.cpp
        src/files/ProfileService.cpp

        # --- src/ui/imgui ---
        src/ui/imgui/ActuationPanel.cpp
        src/ui/imgui/ControllerArt.cpp
        src/ui/imgui/DashboardView.cpp
        src/ui/imgui/DualSenseArt.cpp
        src/ui/imgui/GamepadPanel.cpp
        src/ui/imgui/Glyphs.cpp
        src/ui/imgui/ImGuiTheme.cpp
        src/ui/imgui/KeyboardPage.cpp
        src/ui/imgui/KeyboardView.cpp
        src/ui/imgui/MovementPanel.cpp
        src/ui/imgui/Plots.cpp
        src/ui/imgui/ProfilesPanel.cpp
        src/ui/imgui/SdlGamepadSource.cpp
        src/ui/imgui/SettingsPanel.cpp
        src/ui/imgui/UIManager.cpp
        src/ui/imgui/Widgets.cpp

        # --- src/ui ---
        src/ui/ActuationModel.cpp
        src/ui/AppModel.cpp
        src/ui/AppState.cpp
        src/ui/Color.cpp
        src/ui/ControllerMapping.cpp
        src/ui/DemoGamepadSource.cpp
        src/ui/DemoProfileGateway.cpp
        src/ui/GamepadModel.cpp
        src/ui/KeyboardLayout.cpp
        src/ui/NullTrayIcon.cpp
        src/ui/PngWriter.cpp
        src/ui/ProfileController.cpp
        src/ui/ProfileServiceGateway.cpp
        src/ui/SniTrayIcon.cpp
        src/ui/SystemIntegration.cpp
        src/ui/SystemIntegrationPosix.cpp
        src/ui/SystemIntegrationWin32.cpp
        src/ui/Theme.cpp
        src/ui/ThemeManager.cpp
        src/ui/TrayIconBase.cpp
        src/ui/TrayIconFactory.cpp
        src/ui/TrayIconRenderer.cpp
        src/ui/TrayMenu.cpp
        src/ui/TrayMenuBuilder.cpp
        src/ui/TrayPresenter.cpp
        src/ui/Win32TrayIcon.cpp

        # --- headers/dsa ---
        headers/dsa/DLL.h
        headers/dsa/List.h
        headers/dsa/Node.h

        # --- headers/files ---
        headers/files/AppPaths.h
        headers/files/DirectoryWatcher.h
        headers/files/IFileObserver.h
        headers/files/IniConfig.h
        headers/files/Logger.h
        headers/files/ProfileService.h

        # --- headers/kbd ---
        headers/kbd/DeviceWatcher.h
        headers/kbd/FirmwareModel.h
        headers/kbd/GameController.h
        headers/kbd/HotkeyManager.h
        headers/kbd/IDeviceObserver.h
        headers/kbd/IpcServer.h
        headers/kbd/KbdException.h
        headers/kbd/KeyboardDevice.h
        headers/kbd/KeyboardProfile.h
        headers/kbd/KeychronProtocol.h
        headers/kbd/ProfileMemory.h

        # --- headers/ui/imgui ---
        headers/ui/imgui/ActuationPanel.h
        headers/ui/imgui/ControllerArt.h
        headers/ui/imgui/DashboardView.h
        headers/ui/imgui/GamepadPanel.h
        headers/ui/imgui/Glyphs.h
        headers/ui/imgui/IPanel.h
        headers/ui/imgui/ImGuiTheme.h
        headers/ui/imgui/KeyboardPage.h
        headers/ui/imgui/KeyboardView.h
        headers/ui/imgui/MovementPanel.h
        headers/ui/imgui/Plots.h
        headers/ui/imgui/ProfilesPanel.h
        headers/ui/imgui/SdlGamepadSource.h
        headers/ui/imgui/SettingsPanel.h
        headers/ui/imgui/UIManager.h
        headers/ui/imgui/Widgets.h
        headers/ui/imgui/Workspace.h

        # --- headers/ui ---
        headers/ui/ActuationModel.h
        headers/ui/AppModel.h
        headers/ui/AppState.h
        headers/ui/Color.h
        headers/ui/ControllerMapping.h
        headers/ui/DemoGamepadSource.h
        headers/ui/DemoProfileGateway.h
        headers/ui/EmbeddedAssets.h
        headers/ui/GamepadModel.h
        headers/ui/IProfileCommands.h
        headers/ui/IProfileGateway.h
        headers/ui/ISystemIntegration.h
        headers/ui/IThemeObserver.h
        headers/ui/ITrayIcon.h
        headers/ui/IView.h
        headers/ui/IWindowController.h
        headers/ui/KeyboardLayout.h
        headers/ui/NullTrayIcon.h
        headers/ui/ObserverList.h
        headers/ui/PngWriter.h
        headers/ui/ProfileController.h
        headers/ui/ProfileServiceGateway.h
        headers/ui/SniTrayIcon.h
        headers/ui/SystemIntegration.h
        headers/ui/Theme.h
        headers/ui/ThemeManager.h
        headers/ui/TrayIconBase.h
        headers/ui/TrayIconFactory.h
        headers/ui/TrayIconRenderer.h
        headers/ui/TrayMenu.h
        headers/ui/TrayMenuBuilder.h
        headers/ui/TrayPresenter.h
        headers/ui/Win32TrayIcon.h
)

target_include_directories(KBD_Profile_Loader PRIVATE
        headers
        src
)

target_link_libraries(KBD_Profile_Loader PRIVATE
        kbd_build_options
        hidapi::hidapi
        nlohmann_json::nlohmann_json
        Threads::Threads
        imgui_backend
        OpenGL::GL
)

target_compile_definitions(KBD_Profile_Loader PRIVATE
        $<$<CONFIG:Debug>:KBD_DEBUG_BUILD>
)

if(WIN32 AND CMAKE_HOST_WIN32)
    # Terminate running daemon to prevent LNK1168 file locks before linking.
    # VERBATIM + exit 0 ensures Ninja's cmd.exe wrapper never breaks the link chain.
    add_custom_command(TARGET KBD_Profile_Loader PRE_LINK
            COMMAND powershell -NoProfile -Command "Stop-Process -Name KBD_Profile_Loader -Force -ErrorAction SilentlyContinue; exit 0"
            VERBATIM
            COMMENT "Auto-terminating running daemon to prevent LNK1168 file locks..."
    )
endif()

# ----------------------------------------------------
# 4. GoogleTest Unit Test Target
# ----------------------------------------------------
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

enable_testing()

add_executable(KBD_Unit_Tests
        # --- Test Suites ---
        tests/support/FakeProfileGateway.h
        tests/support/Fakes.h
        tests/support/TempDir.h
        tests/test_actuation.cpp
        tests/test_app_model.cpp
        tests/test_config.cpp
        tests/test_controller_features.cpp
        tests/test_controller_mapping.cpp
        tests/test_dll.cpp
        tests/test_firmware_model.cpp
        tests/test_gamepad_model.cpp
        tests/test_key_actuation.cpp
        tests/test_keyboard_layout.cpp
        tests/test_launcher_profiles.cpp
        tests/test_png_and_demo.cpp
        tests/test_profile_controller.cpp
        tests/test_profile_memory.cpp
        tests/test_profile_service_gateway.cpp
        tests/test_protocol.cpp
        tests/test_sni_tray.cpp
        tests/test_system_integration.cpp
        tests/test_theme.cpp
        tests/test_tray.cpp
        tests/test_win32_tray.cpp

        # --- Tested Implementations ---
        src/files/AppPaths.cpp
        src/files/DirectoryWatcher.cpp
        src/files/IniConfig.cpp
        src/files/Logger.cpp
        src/files/ProfileService.cpp
        src/kbd/DeviceWatcher.cpp
        src/kbd/FirmwareModel.cpp
        src/kbd/GameController.cpp
        src/kbd/HotkeyManager.cpp
        src/kbd/IpcServer.cpp
        src/kbd/KbdException.cpp
        src/kbd/KeyboardDevice.cpp
        src/kbd/KeyboardProfile.cpp
        src/kbd/KeychronProtocol.cpp
        src/kbd/ProfileMemory.cpp
        src/ui/ActuationModel.cpp
        src/ui/AppModel.cpp
        src/ui/AppState.cpp
        src/ui/Color.cpp
        src/ui/ControllerMapping.cpp
        src/ui/DemoGamepadSource.cpp
        src/ui/DemoProfileGateway.cpp
        src/ui/GamepadModel.cpp
        src/ui/KeyboardLayout.cpp
        src/ui/NullTrayIcon.cpp
        src/ui/PngWriter.cpp
        src/ui/ProfileController.cpp
        src/ui/ProfileServiceGateway.cpp
        src/ui/SniTrayIcon.cpp
        src/ui/SystemIntegration.cpp
        src/ui/SystemIntegrationPosix.cpp
        src/ui/SystemIntegrationWin32.cpp
        src/ui/Theme.cpp
        src/ui/ThemeManager.cpp
        src/ui/TrayIconBase.cpp
        src/ui/TrayIconFactory.cpp
        src/ui/TrayIconRenderer.cpp
        src/ui/TrayMenu.cpp
        src/ui/TrayMenuBuilder.cpp
        src/ui/TrayPresenter.cpp
        src/ui/Win32TrayIcon.cpp

        # --- Tested Headers ---
        headers/dsa/DLL.h
        headers/dsa/List.h
        headers/dsa/Node.h
        headers/files/AppPaths.h
        headers/files/DirectoryWatcher.h
        headers/files/IFileObserver.h
        headers/files/IniConfig.h
        headers/files/Logger.h
        headers/files/ProfileService.h
        headers/kbd/DeviceWatcher.h
        headers/kbd/FirmwareModel.h
        headers/kbd/GameController.h
        headers/kbd/HotkeyManager.h
        headers/kbd/IDeviceObserver.h
        headers/kbd/IpcServer.h
        headers/kbd/KbdException.h
        headers/kbd/KeyboardDevice.h
        headers/kbd/KeyboardProfile.h
        headers/kbd/KeychronProtocol.h
        headers/kbd/ProfileMemory.h
        headers/ui/ActuationModel.h
        headers/ui/AppModel.h
        headers/ui/AppState.h
        headers/ui/Color.h
        headers/ui/ControllerMapping.h
        headers/ui/DemoGamepadSource.h
        headers/ui/DemoProfileGateway.h
        headers/ui/EmbeddedAssets.h
        headers/ui/GamepadModel.h
        headers/ui/IProfileCommands.h
        headers/ui/IProfileGateway.h
        headers/ui/ISystemIntegration.h
        headers/ui/IThemeObserver.h
        headers/ui/ITrayIcon.h
        headers/ui/IView.h
        headers/ui/IWindowController.h
        headers/ui/KeyboardLayout.h
        headers/ui/NullTrayIcon.h
        headers/ui/ObserverList.h
        headers/ui/PngWriter.h
        headers/ui/ProfileController.h
        headers/ui/ProfileServiceGateway.h
        headers/ui/SniTrayIcon.h
        headers/ui/SystemIntegration.h
        headers/ui/Theme.h
        headers/ui/ThemeManager.h
        headers/ui/TrayIconBase.h
        headers/ui/TrayIconFactory.h
        headers/ui/TrayIconRenderer.h
        headers/ui/TrayMenu.h
        headers/ui/TrayMenuBuilder.h
        headers/ui/TrayPresenter.h
        headers/ui/Win32TrayIcon.h
)

target_include_directories(KBD_Unit_Tests PRIVATE
        headers
        src
)

target_link_libraries(KBD_Unit_Tests PRIVATE
        kbd_build_options
        GTest::gtest_main
        hidapi::hidapi
        nlohmann_json::nlohmann_json
        Threads::Threads
)

if(CMAKE_CROSSCOMPILING)
    # The test binary cannot run on the build machine, so register it as one test instead of discovering at build time.
    add_test(NAME KBD_Unit_Tests COMMAND KBD_Unit_Tests)
else()
    gtest_discover_tests(KBD_Unit_Tests)
endif()

# UI smoke test (Linux): renders the dashboard against a simulated keyboard on a virtual X display and
# checks that the daemon starts, draws frames, writes the screenshot and shuts down cleanly.
if(UNIX AND NOT APPLE)
    find_program(KBD_XVFB_RUN xvfb-run)
    if(KBD_XVFB_RUN)
        add_test(NAME KBD_UI_Smoke
                COMMAND ${KBD_XVFB_RUN} -a $<TARGET_FILE:KBD_Profile_Loader> --demo --screenshot ${CMAKE_BINARY_DIR}/ui-smoke.png)
        set_tests_properties(KBD_UI_Smoke PROPERTIES TIMEOUT 60 LABELS ui)
    endif()
endif()

# ==============================================================================
# 5. Packaging & Distribution (NSIS, DEB, RPM, TGZ)
# ==============================================================================
include(InstallRequiredSystemLibraries)

if(WIN32)
    install(TARGETS KBD_Profile_Loader DESTINATION . COMPONENT Application)
else()
    install(TARGETS KBD_Profile_Loader DESTINATION bin COMPONENT Application)
    install(FILES packaging/kbd_daemon.service DESTINATION lib/systemd/user COMPONENT Application)
    install(FILES packaging/99-keychron-rules.rules DESTINATION lib/udev/rules.d COMPONENT Application)
endif()

set(CPACK_PACKAGE_NAME "KBD_Profile_Loader")
set(CPACK_PACKAGE_VENDOR "mem64-bits")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Keychron K2 HE Native Profile Daemon")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/mem64-bits/KBD-Profile-Loader")

set(CPACK_COMPONENTS_ALL Application)
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};KBD_Profile_Loader;Application;/")

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "Keychron K2 HE Profile Daemon")
    set(CPACK_NSIS_PACKAGE_NAME "Keychron K2 HE Daemon")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)

    set(CPACK_NSIS_CREATE_ICONS_EXTRA "
        CreateDirectory '$SMPROGRAMS\\\\$STARTMENU_FOLDER'
        CreateShortCut  '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Keychron Daemon.lnk' '$INSTDIR\\\\KBD_Profile_Loader.exe'
        CreateShortCut  '$SMSTARTUP\\\\Keychron Daemon.lnk' '$INSTDIR\\\\KBD_Profile_Loader.exe'
    ")

    set(CPACK_NSIS_DELETE_ICONS_EXTRA "
        Delete '$SMSTARTUP\\\\Keychron Daemon.lnk'
        Delete '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Keychron Daemon.lnk'
        RMDir  '$SMPROGRAMS\\\\$STARTMENU_FOLDER'
    ")
elseif(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    set(CPACK_PACKAGE_CONTACT "mem64-bits@mem64bits.com")

    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "mem64-bits")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libhidapi-hidraw0, udev, systemd, libglib2.0-0")

    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/System")
    set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)
    set(CPACK_RPM_PACKAGE_REQUIRES "hidapi, systemd, udev, glib2")
    set(CPACK_RPM_PACKAGE_RELEASE "1")
endif()
include(CPack)