#pragma once

#if !defined(_WIN32)
#include <string>
#include <thread>
#include <functional>
#include <atomic>

namespace kbd {

    class IpcServer {
    public:
        using CommandHandler = std::function<void(const std::string& cmd)>;

        explicit IpcServer(CommandHandler handler, std::string socketPath = "/tmp/kbd_daemon.sock");
        ~IpcServer();

        void start();
        void stop();

        // Client helper: returns true if message was dispatched to running daemon
        static bool sendCommand(const std::string& cmd, const std::string& socketPath = "/tmp/kbd_daemon.sock");

    private:
        void serverWorker(std::stop_token stopToken);

        CommandHandler    m_handler;
        std::string       m_socketPath;
        std::jthread      m_thread;
        std::atomic<bool> m_running{false};
        int               m_serverFd{-1};
    };

} // namespace kbd
#endif