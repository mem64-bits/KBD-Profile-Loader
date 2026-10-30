#pragma once

#include "ui/IThemeObserver.h"
#include "ui/ObserverList.h"
#include "ui/Theme.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <vector>

namespace ui {

    // Application-wide theme holder (Singleton, like Files::Logger) that publishes theme changes
    // to its observers (Observer, like the file/device watchers).
    class ThemeManager {
    public:
        [[nodiscard]] static ThemeManager& getInstance();

        // Thread-safe snapshot of the active theme.
        [[nodiscard]] Theme getTheme() const;

        // Incremented on every change; lets a render loop cheaply notice that it must re-apply the theme.
        [[nodiscard]] std::uint64_t revision() const noexcept;

        // No-op (and no notification) if `theme` equals the active one.
        void setTheme(Theme theme);
        void resetToDefault();

        // Loads a theme.json. On failure the active theme is left untouched. Non-fatal problems
        // (unknown keys, bad colours) are appended to `issues` and the rest of the file still applies.
        bool loadFromFile(const std::filesystem::path& path, std::vector<ThemeIssue>* issues = nullptr);
        bool saveToFile(const std::filesystem::path& path) const;

        // Re-reads the file last passed to loadFromFile() if it was modified since (hot reload).
        bool reloadIfChanged();

        void addObserver(IThemeObserver* observer);
        void removeObserver(IThemeObserver* observer);

    private:
        ThemeManager() = default;
        ~ThemeManager() = default;

        ThemeManager(const ThemeManager&) = delete;
        ThemeManager& operator=(const ThemeManager&) = delete;
        ThemeManager(ThemeManager&&) = delete;
        ThemeManager& operator=(ThemeManager&&) = delete;

        mutable std::mutex           m_mutex;
        Theme                        m_theme{Theme::blackPurple()};
        std::filesystem::path        m_sourcePath;
        std::filesystem::file_time_type m_sourceTime{};
        std::atomic<std::uint64_t>   m_revision{0};
        ObserverList<IThemeObserver> m_observers;
    };

} // namespace ui
