#if defined(_WIN32)

#include "ui/SystemIntegration.h"
#include "files/AppPaths.h"
#include "files/Logger.h"

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
// This file calls the wide (W) Win32 functions explicitly; UNICODE makes macros such as IDI_APPLICATION
// resolve to their wide variants too, instead of the ANSI ones.
#ifndef UNICODE
    #define UNICODE
#endif
#ifndef _UNICODE
    #define _UNICODE
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>

#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace ui {

namespace {

    // Same shortcut name the installer (CPack/NSIS) puts into the Startup folder, so both agree.
    constexpr const wchar_t* STARTUP_SHORTCUT_NAME = L"Keychron Daemon.lnk";

    fs::path startupShortcutPath() {
        return Files::AppPaths::getWindowsStartupDir() / STARTUP_SHORTCUT_NAME;
    }

    fs::path currentExecutable() {
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;) {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0) return {};
            if (length < buffer.size()) {
                buffer.resize(length);
                return fs::path(buffer);
            }
            buffer.resize(buffer.size() * 2); // path longer than the buffer: grow and retry
        }
    }

    bool createShortcut(const fs::path& link, const fs::path& target) {
        const HRESULT initialised = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const bool mustUninitialise = SUCCEEDED(initialised); // S_FALSE also needs a matching CoUninitialize

        IShellLinkW* shellLink = nullptr;
        HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                          reinterpret_cast<void**>(&shellLink));
        if (SUCCEEDED(result)) {
            shellLink->SetPath(target.c_str());
            shellLink->SetWorkingDirectory(target.parent_path().c_str());
            shellLink->SetDescription(L"Keychron K2 HE profile daemon");

            IPersistFile* file = nullptr;
            result = shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file));
            if (SUCCEEDED(result)) {
                result = file->Save(link.c_str(), TRUE);
                file->Release();
            }
            shellLink->Release();
        }

        if (mustUninitialise) CoUninitialize();
        return SUCCEEDED(result);
    }

} // namespace

void SystemIntegration::openPath(const fs::path& path) {
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) { // ShellExecute reports errors as values <= 32
        LOG_WARN("System", "Could not open " + path.string());
    }
}

fs::path SystemIntegration::resolveExecutable() const {
    return m_executablePath.empty() ? currentExecutable() : m_executablePath;
}

bool SystemIntegration::isAutostartEnabled() const {
    std::error_code ec;
    return fs::exists(startupShortcutPath(), ec);
}

bool SystemIntegration::setAutostartEnabled(const bool enabled) {
    const fs::path shortcut = startupShortcutPath();
    std::error_code ec;

    if (!enabled) {
        fs::remove(shortcut, ec);
        if (ec) {
            LOG_WARN("System", "Could not remove the Startup shortcut: " + ec.message());
            return false;
        }
        LOG_INFO("System", "Disabled Windows auto-start (removed the Startup shortcut).");
        return true;
    }

    const fs::path executable = resolveExecutable();
    if (executable.empty() || !createShortcut(shortcut, executable)) {
        LOG_WARN("System", "Could not create the Startup shortcut.");
        return false;
    }
    LOG_INFO("System", "Enabled Windows auto-start (created a shortcut in the Startup folder).");
    return true;
}

} // namespace ui

#endif // _WIN32
