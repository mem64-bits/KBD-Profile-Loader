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

# GLFW
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(external/glfw)

# ----------------------------------------------------
# 2. Compile Dear ImGui + Backends Target
# ----------------------------------------------------
add_library(imgui_backend STATIC
        external/imgui/imgui.cpp
        external/imgui/imgui_draw.cpp
        external/imgui/imgui_tables.cpp
        external/imgui/imgui_widgets.cpp
        external/imgui/backends/imgui_impl_glfw.cpp
        external/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui_backend PUBLIC
        external/imgui
        external/imgui/backends
        external/glfw/include
)

target_link_libraries(imgui_backend PUBLIC glfw OpenGL::GL)

# ----------------------------------------------------
# 3. Main Daemon Executable Target
# ----------------------------------------------------
add_executable(KBD_Profile_Loader
        # --- src/main ---
        src/main/main.cpp

        # --- src/kbd ---
        src/kbd/DeviceWatcher.cpp
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
        src/ui/imgui/DashboardView.cpp
        src/ui/imgui/ImGuiTheme.cpp
        src/ui/imgui/ProfilesPanel.cpp
        src/ui/imgui/SettingsPanel.cpp
        src/ui/imgui/UIManager.cpp
        src/ui/imgui/Widgets.cpp

        # --- src/ui ---
        src/ui/ActuationModel.cpp
        src/ui/AppModel.cpp
        src/ui/AppState.cpp
        src/ui/Color.cpp
        src/ui/DemoProfileGateway.cpp
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
        headers/ui/imgui/DashboardView.h
        headers/ui/imgui/IPanel.h
        headers/ui/imgui/ImGuiTheme.h
        headers/ui/imgui/ProfilesPanel.h
        headers/ui/imgui/SettingsPanel.h
        headers/ui/imgui/UIManager.h
        headers/ui/imgui/Widgets.h

        # --- headers/ui ---
        headers/ui/ActuationModel.h
        headers/ui/AppModel.h
        headers/ui/AppState.h
        headers/ui/Color.h
        headers/ui/DemoProfileGateway.h
        headers/ui/IProfileCommands.h
        headers/ui/IProfileGateway.h
        headers/ui/ISystemIntegration.h
        headers/ui/IThemeObserver.h
        headers/ui/ITrayIcon.h
        headers/ui/IView.h
        headers/ui/IWindowController.h
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
include(FetchContent)
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
        tests/test_dll.cpp
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
        src/ui/DemoProfileGateway.cpp
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
        headers/ui/DemoProfileGateway.h
        headers/ui/IProfileCommands.h
        headers/ui/IProfileGateway.h
        headers/ui/ISystemIntegration.h
        headers/ui/IThemeObserver.h
        headers/ui/ITrayIcon.h
        headers/ui/IView.h
        headers/ui/IWindowController.h
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