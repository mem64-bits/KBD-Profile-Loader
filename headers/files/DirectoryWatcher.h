#pragma once

#include "dsa/DLL.h"

#include <filesystem>
#include <thread>
#include <mutex>
#include <unordered_set>
#include <string>

namespace Files {

    namespace fs = std::filesystem;

    // Forward declaration
    class IFileObserver;

    class DirectoryWatcher {
    public:
        explicit DirectoryWatcher(fs::path pathToWatch);
        ~DirectoryWatcher();

        DirectoryWatcher(const DirectoryWatcher&) = delete;
        DirectoryWatcher& operator=(const DirectoryWatcher&) = delete;

        void addObserver(IFileObserver* observer);
        void removeObserver(IFileObserver* observer);

        void start();
        void stop();

        [[nodiscard]] const fs::path& getWatchPath() const noexcept;

    private:
        void run(const std::stop_token& stopToken);
        void notifyFileCreated(const fs::path& path);
        void notifyFileDeleted(const fs::path& path);

        dsa::DLL<IFileObserver*>        m_observers;
        fs::path                        m_watchPath{};
        std::jthread                    m_thread{};
        std::mutex                      m_observerMutex{};
        std::unordered_set<std::string> m_knownFiles{};
    };

} // namespace files
