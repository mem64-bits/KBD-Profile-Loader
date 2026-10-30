#include "ui/SystemIntegration.h"
#include "files/AppPaths.h"

#include <utility>

namespace ui {

SystemIntegration::SystemIntegration(std::filesystem::path executablePath)
    : m_executablePath(std::move(executablePath))
{}

SystemIntegration::~SystemIntegration() = default;

void SystemIntegration::openProfilesFolder() {
    openPath(Files::AppPaths::getProfilesDir());
}

void SystemIntegration::openConfigFile() {
    openPath(Files::AppPaths::getConfigFilePath());
}

void SystemIntegration::openLogFile() {
    openPath(Files::AppPaths::getLogFilePath());
}

} // namespace ui
