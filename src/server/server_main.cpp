#include "logger.hpp"
#include "protocol.hpp"
#include "server_logic.hpp"
#include "server_net.hpp"

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

ServerNet *g_server_net = nullptr;

void handle_signal(int /*sig*/) {
    if (g_server_net != nullptr) {
        g_server_net->stop();
    }
}

void print_usage(const char *prog) {
    std::cerr << "Usage: " << prog << " [--host <ip>] [--port <port>] [--log <path>] [port]\n";
}

bool parse_port(const std::string &str, std::uint16_t &out_port) {
    if (str.empty()) {
        return false;
    }
    char *end = nullptr;
    unsigned long val = std::strtoul(str.c_str(), &end, 10);
    if (*end != '\0' || val == 0 || val > 65535) {
        return false;
    }
    out_port = static_cast<std::uint16_t>(val);
    return true;
}

} // namespace

int main(int argc, char *argv[]) {
    std::string host(roborace::protocol::DEFAULT_HOST);
    std::uint16_t port = roborace::protocol::DEFAULT_PORT;
    std::string log_path = "events.log";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--host" || arg == "-h") && i + 1 < argc) {
            host = argv[++i];
        } else if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
            if (!parse_port(argv[++i], port)) {
                std::cerr << "Invalid port: " << argv[i] << "\n";
                return 1;
            }
        } else if ((arg == "--log" || arg == "-l") && i + 1 < argc) {
            log_path = argv[++i];
        } else if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (!arg.empty() && arg[0] != '-') {
            if (!parse_port(arg, port)) {
                print_usage(argv[0]);
                return 1;
            }
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    Logger logger;
    if (!logger.open(log_path)) {
        std::cerr << "Failed to open log file: " << log_path << "\n";
        return 1;
    }

    ServerNet net;
    if (!net.start(host, port)) {
        std::cerr << "Failed to start server on " << host << ":" << port << "\n";
        return 1;
    }

    g_server_net = &net;
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    ServerLogic logic(net, logger);
    net.run(
        [&logic](int client_fd, const std::string &line) { logic.on_line(client_fd, line); },
        [&logic](int client_fd) { logic.on_disconnect(client_fd); }
    );

    g_server_net = nullptr;
    net.shutdown();
    logger.close();
    return 0;
}
