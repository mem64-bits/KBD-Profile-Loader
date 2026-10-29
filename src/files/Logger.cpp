#include "files/Logger.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace Files {

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

Logger::~Logger() {
    if (m_logFile.is_open()) {
        m_logFile.flush();
        m_logFile.close();
    }
}

void Logger::init(const std::filesystem::path& logPath, bool echoToConsole) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_echoToConsole = echoToConsole;

    if (logPath.has_parent_path() && !std::filesystem::exists(logPath.parent_path())) {
        std::filesystem::create_directories(logPath.parent_path());
    }

    // Append mode with clean session separator
    m_logFile.open(logPath, std::ios::app);
    m_isInitialized = m_logFile.is_open();

    if (m_isInitialized) {
        m_logFile << "\n========================================================\n"
                  << "  DAEMON SESSION START: " << getCurrentTimestamp() << "\n"
                  << "========================================================\n";
        m_logFile.flush();
    }
}

std::string Logger::getCurrentTimestamp() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    auto timer = system_clock::to_time_t(now);

    std::tm bt{};
#if defined(_WIN32)
    localtime_s(&bt, &timer);
#else
    localtime_r(&timer, &bt);
#endif

    std::ostringstream oss;
    oss << std::put_time(&bt, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

void Logger::log(LogLevel level, std::string_view tag, std::string_view message) {
    std::lock_guard<std::mutex> lock(m_mutex);

    const char* levelStr = "INFO";
    switch (level) {
        case LogLevel::Debug:   levelStr = "DEBUG"; break;
        case LogLevel::Info:    levelStr = "INFO";  break;
        case LogLevel::Warning: levelStr = "WARN";  break;
        case LogLevel::Error:   levelStr = "ERROR"; break;
    }

    std::string timestamp = getCurrentTimestamp();

    if (m_isInitialized) {
        m_logFile << "[" << timestamp << "] [" << levelStr << "] [" << tag << "] "
                  << message << "\n";
        m_logFile.flush();
    }

    if (m_echoToConsole) {
        std::ostream& out = (level == LogLevel::Error) ? std::cerr : std::cout;
        out << "[" << timestamp << "] [" << levelStr << "] [" << tag << "] "
            << message << "\n";
    }
}

} // namespace Files