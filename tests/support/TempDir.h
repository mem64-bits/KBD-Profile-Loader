#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

namespace test {

    // Creates a unique scratch directory and removes it (recursively) on destruction.
    class TempDir {
    public:
        TempDir() {
            static std::atomic<unsigned> counter{0};
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            m_path = std::filesystem::temp_directory_path() /
                     ("kbd_tests_" + std::to_string(stamp) + "_" + std::to_string(counter++));
            std::filesystem::create_directories(m_path);
        }

        ~TempDir() {
            std::error_code ec;
            std::filesystem::remove_all(m_path, ec);
        }

        TempDir(const TempDir&) = delete;
        TempDir& operator=(const TempDir&) = delete;
        TempDir(TempDir&&) = delete;
        TempDir& operator=(TempDir&&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }
        [[nodiscard]] std::filesystem::path operator/(const std::string& name) const { return m_path / name; }

    private:
        std::filesystem::path m_path;
    };

} // namespace test
