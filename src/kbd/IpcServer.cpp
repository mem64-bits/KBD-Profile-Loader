#if !defined(_WIN32)
#include "kbd/IpcServer.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>
#include <iostream>
#include <cstring>

namespace kbd {

IpcServer::IpcServer(CommandHandler handler, std::string socketPath)
    : m_handler(std::move(handler)), m_socketPath(std::move(socketPath))
{}

IpcServer::~IpcServer() {
    stop();
}

void IpcServer::start() {
    if (m_running.load(std::memory_order_acquire)) return;
    m_running.store(true, std::memory_order_release);

    m_thread = std::jthread([this](std::stop_token stopToken) {
        serverWorker(stopToken);
    });
}

void IpcServer::stop() {
    if (!m_running.exchange(false, std::memory_order_acq_rel)) return;

    if (m_serverFd != -1) {
        close(m_serverFd);
        m_serverFd = -1;
    }
    unlink(m_socketPath.c_str());

    if (m_thread.joinable()) {
        m_thread.request_stop();
        m_thread.join();
    }
}

bool IpcServer::sendCommand(const std::string& cmd, const std::string& socketPath) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return false;

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(sock);
        return false;
    }

    std::string payload = cmd + "\n";
    write(sock, payload.data(), payload.size());
    close(sock);
    return true;
}

void IpcServer::serverWorker(std::stop_token stopToken) {
    unlink(m_socketPath.c_str()); // Clean up stale socket

    m_serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_serverFd < 0) return;

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, m_socketPath.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(m_serverFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(m_serverFd);
        return;
    }

    listen(m_serverFd, 5);
    std::cout << "[IPC Server] Wayland IPC socket listening on: " << m_socketPath << '\n';

    while (!stopToken.stop_requested()) {
        struct pollfd pfd{ m_serverFd, POLLIN, 0 };
        int pollRes = poll(&pfd, 1, 250); // 250ms check for clean thread shutdown

        if (pollRes > 0 && (pfd.revents & POLLIN)) {
            int clientFd = accept(m_serverFd, nullptr, nullptr);
            if (clientFd >= 0) {
                char buffer[64] = {0};
                ssize_t bytes = read(clientFd, buffer, sizeof(buffer) - 1);
                if (bytes > 0) {
                    std::string message(buffer);
                    // Trim newline
                    while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
                        message.pop_back();
                    }
                    if (m_handler) m_handler(message);
                }
                close(clientFd);
            }
        }
    }

    close(m_serverFd);
    m_serverFd = -1;
    unlink(m_socketPath.c_str());
}

} // namespace kbd
#endif