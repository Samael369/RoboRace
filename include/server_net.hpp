#pragma once

#include "net_buffer.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

struct ClientConnection {
    int fd = -1;
    NetBuffer rx_buf;
};

class ServerNet {
  public:
    using LineCallback = std::function<void(int client_fd, const std::string &line)>;
    using DisconnectCallback = std::function<void(int client_fd)>;

    ServerNet() = default;
    ~ServerNet();

    ServerNet(const ServerNet &) = delete;
    ServerNet &operator=(const ServerNet &) = delete;

    bool start(const std::string &host, std::uint16_t port);
    void run(const LineCallback &on_line, const DisconnectCallback &on_disconnect);
    void stop();

    bool send_line(int client_fd, const std::string &line);
    void close_client(int client_fd);
    void shutdown();

  private:
    int listen_fd_ = -1;
    bool running_ = false;
    std::unordered_map<int, ClientConnection> clients_;

    void accept_new_clients();
    bool handle_client_read(int client_fd, const LineCallback &on_line);
};
