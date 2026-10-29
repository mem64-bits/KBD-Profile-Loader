#include "files/DirectoryWatcher.h"
#include "files/IFileObserver.h" // Complete type included here
#include <chrono>
#include <iostream>
#include <utility>

using namespace std::chrono_literals;

Files::DirectoryWatcher::DirectoryWatcher(fs::path pathToWatch)
    : m_watchPath(std::move(pathToWatch))
{
    if (!fs::exists(m_watchPath)) {
        fs::create_directories(m_watchPath);
    }

    for (const auto& entry : fs::directory_iterator(m_watchPath)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            m_knownFiles.insert(entry.path().filename().string());
        }
    }
}

Files::DirectoryWatcher::~DirectoryWatcher() {
    stop();
}

void Files::DirectoryWatcher::addObserver(IFileObserver* observer) {
    std::lock_guard<std::mutex> lock(m_observerMutex);
    if (observer != nullptr && !m_observers.contains(observer)) {
        m_observers.addLast(observer);
    }
}

void Files::DirectoryWatcher::removeObserver(IFileObserver* observer) {
    std::lock_guard<std::mutex> lock(m_observerMutex);
    m_observers.remove(observer);
}

void Files::DirectoryWatcher::start() {
    if (m_thread.joinable()) return;

    m_thread = std::jthread([this](const std::stop_token& stopToken) {
        run(stopToken);
    });

    std::cout << "[DirectoryWatcher] Background thread started on: " << m_watchPath.string() << '\n';
}

void Files::DirectoryWatcher::stop() {
    if (m_thread.joinable()) {
        m_thread.request_stop();
        m_thread.join();
        std::cout << "[DirectoryWatcher] Background thread stopped.\n";
    }
}

const std::filesystem::path& Files::DirectoryWatcher::getWatchPath() const noexcept {
    return m_watchPath;
}

void Files::DirectoryWatcher::run(const std::stop_token& stopToken) {
    while (!stopToken.stop_requested()) {
        std::this_thread::sleep_for(1s);

        if (stopToken.stop_requested() || !fs::exists(m_watchPath)) break;

        std::unordered_set<std::string> currentFilesOnDisk;

        for (const auto& entry : fs::directory_iterator(m_watchPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::string filename = entry.path().filename().string();
                currentFilesOnDisk.insert(filename);

                if (!m_knownFiles.contains(filename)) {
                    notifyFileCreated(entry.path());
                }
            }
        }

        for (const auto& known : m_knownFiles) {
            if (!currentFilesOnDisk.contains(known)) {
                notifyFileDeleted(m_watchPath / known);
            }
        }

        m_knownFiles = std::move(currentFilesOnDisk);
    }
}

void Files::DirectoryWatcher::notifyFileCreated(const fs::path& path) {
    std::lock_guard<std::mutex> lock(m_observerMutex);
    m_observers.forEach([&path](IFileObserver* observer) {
        if (observer != nullptr) observer->onFileCreated(path);
    });
}

void Files::DirectoryWatcher::notifyFileDeleted(const fs::path& path) {
    std::lock_guard<std::mutex> lock(m_observerMutex);
    m_observers.forEach([&path](Files::IFileObserver* observer) {
        if (observer != nullptr) observer->onFileDeleted(path);
    });
}
