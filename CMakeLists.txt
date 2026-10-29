cmake_minimum_required(VERSION 3.25)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(KBD_Profile_Loader VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)

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

        # --- src/files ---
        src/files/AppPaths.cpp
        src/files/DirectoryWatcher.cpp
        src/files/IniConfig.cpp
        src/files/Logger.cpp
        src/files/ProfileService.cpp

        # --- src ---
        src/ui/TrayManager.cpp
        src/ui/UIManager.cpp
        src/ui/ViewProfilePickerView.cpp

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

        # --- headers ---
        headers/ui/IView.h
        headers/ui/QuickProfilePickerView.h
        headers/ui/TrayManager.h
        headers/ui/UIManager.h
)

target_include_directories(KBD_Profile_Loader PRIVATE
        headers
        src
)

target_link_libraries(KBD_Profile_Loader PRIVATE
        hidapi
        nlohmann_json::nlohmann_json
        Threads::Threads
        imgui_backend
        OpenGL::GL
)

target_compile_definitions(KBD_Profile_Loader PRIVATE
        $<$<CONFIG:Debug>:KBD_DEBUG_BUILD>
)

if(WIN32)
    # Terminate running daemon to prevent LNK1168 file locks before linking
    add_custom_command(TARGET KBD_Profile_Loader PRE_LINK
            COMMAND cmd /c "taskkill /F /IM KBD_Profile_Loader.exe >nul 2>&1 || exit /b 0"
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
        tests/test_config.cpp
        tests/test_dll.cpp
        tests/test_protocol.cpp

        # --- Tested Implementations ---
        src/kbd/KeychronProtocol.cpp
        src/files/IniConfig.cpp
        src/files/Logger.cpp
        src/files/AppPaths.cpp

        # --- Tested Headers ---
        headers/dsa/Node.h
        headers/dsa/List.h
        headers/dsa/DLL.h
        headers/files/IniConfig.h
        headers/files/Logger.h
        headers/files/AppPaths.h
        headers/kbd/KeychronProtocol.h
        headers/kbd/KbdException.h
)

target_include_directories(KBD_Unit_Tests PRIVATE
        headers
        src
)

target_link_libraries(KBD_Unit_Tests PRIVATE
        GTest::gtest_main
        nlohmann_json::nlohmann_json
)

gtest_discover_tests(KBD_Unit_Tests)

# ==============================================================================
# 5. Packaging & Distribution (NSIS, DEB, RPM, TGZ)
# ==============================================================================
include(InstallRequiredSystemLibraries)

if(WIN32)
    install(TARGETS KBD_Profile_Loader DESTINATION . COMPONENT Application)
else()
    install(TARGETS KBD_Profile_Loader DESTINATION bin COMPONENT Application)
    install(FILES packaging/kbd-daemon.service DESTINATION lib/systemd/user COMPONENT Application)
    install(FILES packaging/99-keychron.rules DESTINATION lib/udev/rules.d COMPONENT Application)
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
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libhidapi-hidraw0, udev, systemd")

    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/System")
    set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)
    set(CPACK_RPM_PACKAGE_REQUIRES "hidapi, systemd, udev")
    set(CPACK_RPM_PACKAGE_RELEASE "1")
endif()

include(CPack)