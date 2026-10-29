#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <filesystem>
#include <string_view>

namespace Files {

    enum class LogLevel {
        Debug,
        Info,
        Warning,
        Error
    };

    class Logger {
    public:
        static Logger& getInstance();

        void init(const std::filesystem::path& logPath, bool echoToConsole = false);
        void log(LogLevel level, std::string_view tag, std::string_view message);

        void info(const std::string_view tag, std::string_view message)    { log(LogLevel::Info, tag, message); }
        void warning(std::string_view tag, std::string_view message) { log(LogLevel::Warning, tag, message); }
        void error(std::string_view tag, std::string_view message)   { log(LogLevel::Error, tag, message); }
        void debug(std::string_view tag, std::string_view message)   { log(LogLevel::Debug, tag, message); }

    private:
        Logger() = default;
        ~Logger();

        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        std::string getCurrentTimestamp();

        std::ofstream      m_logFile;
        std::mutex         m_mutex;
        bool               m_echoToConsole = false;
        bool               m_isInitialized = false;
    };

} // namespace Files

#define LOG_INFO(tag, msg)    Files::Logger::getInstance().info(tag, msg)
#define LOG_WARN(tag, msg)    Files::Logger::getInstance().warning(tag, msg)
#define LOG_ERROR(tag, msg)   Files::Logger::getInstance().error(tag, msg)
#define LOG_DEBUG(tag, msg)   Files::Logger::getInstance().debug(tag, msg)