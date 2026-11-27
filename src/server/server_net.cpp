#include "server_net.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <vector>

ServerNet::~ServerNet() {
    shutdown();
}

bool ServerNet::start(const std::string &host, std::uint16_t port) {
    // 1. Ignore SIGPIPE to avoid process crashes on client disconnects
    std::signal(SIGPIPE, SIG_IGN);

    // 2. Create TCP socket
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        return false;
    }

    // 3. Set SO_REUSEADDR
    int opt = 1;
    if (::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    // 4. Set O_NONBLOCK on listen_fd_
    int flags = ::fcntl(listen_fd_, F_GETFL, 0);
    if (flags < 0 || ::fcntl(listen_fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    // 5. Bind host:port
    sockaddr_in addr{};
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (::bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    // 6. Listen
    if (::listen(listen_fd_, SOMAXCONN) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    running_ = true;
    return true;
}

void ServerNet::stop() {
    running_ = false;
}

void ServerNet::accept_new_clients() {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = ::accept(listen_fd_, reinterpret_cast<sockaddr *>(&client_addr), &addr_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Done reading incoming connection queue
                break;
            }
            if (errno == EINTR) {
                continue; // Interrupted by signal, retry
            }
            // Fatal accept error
            break;
        }

        // Set client socket to non-blocking
        int flags = ::fcntl(client_fd, F_GETFL, 0);
        if (flags >= 0) {
            ::fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
        }

        clients_.emplace(client_fd, ClientConnection{client_fd, NetBuffer{}});
    }
}

bool ServerNet::handle_client_read(int client_fd, const LineCallback &on_line) {
    auto it = clients_.find(client_fd);
    if (it == clients_.end()) {
        return false;
    }

    ClientConnection &conn = it->second;
    char buffer[4096];

    while (true) {
        ssize_t bytes_read = ::recv(client_fd, buffer, sizeof(buffer), 0);

        if (bytes_read == 0) {
            // EOF / Graceful disconnect
            return false;
        }

        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No more bytes to read right now
                break;
            }
            if (errno == EINTR) {
                continue; // Interrupted, retry reading
            }
            // Fatal socket error
            return false;
        }

        // Feed bytes into buffer
        conn.rx_buf.append(buffer, static_cast<std::size_t>(bytes_read));
    }

    // Extract complete lines
    std::string line;
    while (true) {
        LineStatus status = conn.rx_buf.pop_line(line);
        if (status == LineStatus::OK) {
            on_line(client_fd, line);
        } else if (status == LineStatus::NEED_MORE) {
            break;
        } else if (status == LineStatus::TOO_LONG) {
            return false; // Disconnect misbehaving client
        }
    }

    return true;
}

void ServerNet::run(const LineCallback &on_line, const DisconnectCallback &on_disconnect) {
    while (running_) {
        std::vector<pollfd> poll_fds;
        poll_fds.reserve(1 + clients_.size());

        // Add listening socket
        if (listen_fd_ >= 0) {
            poll_fds.push_back({listen_fd_, POLLIN, 0});
        }

        // Add all active client sockets
        for (const auto &pair : clients_) {
            poll_fds.push_back({pair.first, POLLIN, 0});
        }

        int ret = ::poll(poll_fds.data(), poll_fds.size(), 100);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (ret == 0) {
            continue; // Timeout
        }

        // 1. Check listen_fd_
        std::size_t start_idx = 0;
        if (listen_fd_ >= 0 && !poll_fds.empty() && poll_fds[0].fd == listen_fd_) {
            if (poll_fds[0].revents & POLLIN) {
                accept_new_clients();
            }
            start_idx = 1;
        }

        // 2. Check client sockets (collect dead fds first to avoid modifying clients_
        // mid-iteration)
        std::vector<int> dead_clients;

        for (std::size_t i = start_idx; i < poll_fds.size(); ++i) {
            const auto &pfd = poll_fds[i];
            if (pfd.revents & (POLLIN | POLLERR | POLLHUP)) {
                if (!handle_client_read(pfd.fd, on_line)) {
                    dead_clients.push_back(pfd.fd);
                }
            }
        }

        // Clean up disconnected clients
        for (int fd : dead_clients) {
            on_disconnect(fd);
            close_client(fd);
        }
    }
}

bool ServerNet::send_line(int client_fd, const std::string &line) {
    std::string payload = line;
    if (payload.empty() || payload.back() != '\n') {
        payload.push_back('\n');
    }

    const char *data = payload.data();
    std::size_t total_sent = 0;
    std::size_t total_len = payload.size();

    while (total_sent < total_len) {
        ssize_t sent = ::send(client_fd, data + total_sent, total_len - total_sent, 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue; // Interrupted by signal, retry
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // If send buffers are full, yield or wait; fail if non-blocking write can't proceed
                return false;
            }
            return false;
        }
        total_sent += static_cast<std::size_t>(sent);
    }

    return true;
}

void ServerNet::close_client(int client_fd) {
    auto it = clients_.find(client_fd);
    if (it != clients_.end()) {
        if (it->first >= 0) {
            ::close(it->first);
        }
        clients_.erase(it);
    }
}

void ServerNet::shutdown() {
    stop();

    // Close and remove all clients
    for (auto &pair : clients_) {
        if (pair.first >= 0) {
            ::close(pair.first);
        }
    }
    clients_.clear();

    // Close listening socket
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
}
