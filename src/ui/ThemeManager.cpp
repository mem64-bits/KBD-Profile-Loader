#include "ui/ThemeManager.h"
#include "files/Logger.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace ui {

ThemeManager& ThemeManager::getInstance() {
    static ThemeManager instance;
    return instance;
}

Theme ThemeManager::getTheme() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_theme;
}

std::uint64_t ThemeManager::revision() const noexcept {
    return m_revision.load(std::memory_order_acquire);
}

void ThemeManager::setTheme(Theme theme) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (theme == m_theme) return;
        m_theme = theme;
        m_revision.fetch_add(1, std::memory_order_acq_rel);
    }

    // Notify outside m_mutex so an observer may call getTheme() from its callback.
    m_observers.notify([&theme](IThemeObserver& observer) { observer.onThemeChanged(theme); });
}

void ThemeManager::resetToDefault() {
    setTheme(Theme::blackPurple());
}

bool ThemeManager::loadFromFile(const fs::path& path, std::vector<ThemeIssue>* issues) {
    std::ifstream file(path);
    if (!file.is_open()) {
        if (issues != nullptr) issues->push_back({"", "Could not open " + path.string()});
        return false;
    }

    const nlohmann::json json = nlohmann::json::parse(file, nullptr, false);
    if (json.is_discarded()) {
        LOG_WARN("Theme", "Ignoring " + path.filename().string() + ": it is not valid JSON.");
        if (issues != nullptr) issues->push_back({"", path.filename().string() + " is not valid JSON."});
        return false;
    }

    std::vector<ThemeIssue> localIssues;
    const auto theme = Theme::fromJson(json, &localIssues);
    for (const auto& issue : localIssues) {
        LOG_WARN("Theme", path.filename().string() + ": " + issue.field + (issue.field.empty() ? "" : ": ") + issue.message);
    }
    if (issues != nullptr) issues->insert(issues->end(), localIssues.begin(), localIssues.end());
    if (!theme) return false;

    std::error_code ec;
    const auto modified = fs::last_write_time(path, ec);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sourcePath = path;
        m_sourceTime = ec ? fs::file_time_type{} : modified;
    }

    setTheme(*theme);
    return true;
}

bool ThemeManager::saveToFile(const fs::path& path) const {
    const Theme theme = getTheme();

    try {
        if (path.has_parent_path()) fs::create_directories(path.parent_path());

        // Write-then-rename so a hot-reloading reader never sees a half-written file.
        fs::path temporary = path;
        temporary += ".tmp";
        {
            std::ofstream file(temporary, std::ios::trunc);
            if (!file.is_open()) return false;
            file << theme.toJson().dump(2) << '\n';
            if (!file.good()) return false;
        }
        fs::rename(temporary, path);
        return true;
    } catch (const std::exception& e) {
        LOG_WARN("Theme", std::string("Could not save theme: ") + e.what());
        return false;
    }
}

bool ThemeManager::reloadIfChanged() {
    fs::path path;
    fs::file_time_type known;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        path  = m_sourcePath;
        known = m_sourceTime;
    }
    if (path.empty()) return false;

    std::error_code ec;
    const auto current = fs::last_write_time(path, ec);
    if (ec || current == known) return false;

    {
        // Remember the timestamp first: a file that is mid-edit (invalid JSON) is retried
        // only after it changes again, instead of re-parsing and logging every poll.
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sourceTime = current;
    }
    return loadFromFile(path);
}

void ThemeManager::addObserver(IThemeObserver* observer) {
    m_observers.add(observer);
}

void ThemeManager::removeObserver(IThemeObserver* observer) {
    m_observers.remove(observer);
}

} // namespace ui
