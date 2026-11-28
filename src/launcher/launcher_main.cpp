#include "protocol.hpp"
#include "scenario.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <poll.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using namespace roborace::protocol;

volatile std::sig_atomic_t g_interrupted = 0;

void handle_signal(int) {
    g_interrupted = 1;
}

pid_t spawn_process(const std::vector<std::string> &args, int close_fd_in_child = -1) {
    pid_t pid = ::fork();
    if (pid < 0) {
        std::perror("fork failed");
        return -1;
    }
    if (pid == 0) {
        if (close_fd_in_child >= 0) {
            ::close(close_fd_in_child);
        }
        std::vector<std::string> mutable_args = args;
        std::vector<char *> argv;
        argv.reserve(mutable_args.size() + 1);
        for (auto &s : mutable_args) {
            argv.push_back(s.data());
        }
        argv.push_back(nullptr);

        ::execvp(argv[0], argv.data());
        std::perror("execvp failed");
        std::_Exit(127);
    }
    return pid;
}

bool read_pipe_line(int fd, std::string &out_line, int timeout_ms) {
    out_line.clear();
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (true) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return false;
        }
        int remaining_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count()
        );

        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;
        int rc = ::poll(&pfd, 1, remaining_ms);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (rc == 0) {
            return false; // timeout
        }

        char ch = 0;
        ssize_t n = ::read(fd, &ch, 1);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            return !out_line.empty();
        }
        if (ch == '\n') {
            return true;
        }
        if (ch != '\r') {
            out_line.push_back(ch);
        }
    }
}

void kill_and_reap(std::vector<pid_t> &pids) {
    for (pid_t pid : pids) {
        ::kill(pid, SIGTERM);
    }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!pids.empty() && std::chrono::steady_clock::now() < deadline) {
        int status = 0;
        pid_t done = ::waitpid(-1, &status, WNOHANG);
        if (done > 0) {
            pids.erase(std::remove(pids.begin(), pids.end(), done), pids.end());
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    for (pid_t pid : pids) {
        ::kill(pid, SIGKILL);
    }
    while (!pids.empty()) {
        int status = 0;
        pid_t done = ::waitpid(-1, &status, 0);
        if (done > 0) {
            pids.erase(std::remove(pids.begin(), pids.end(), done), pids.end());
        } else if (done < 0 && errno == ECHILD) {
            pids.clear();
        }
    }
}

} // namespace

int main(int argc, char *argv[]) {
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <scenario.race> [port]\n";
        return 1;
    }

    std::string scenario_path = argv[1];
    std::uint16_t port = DEFAULT_PORT;
    if (argc >= 3) {
        port = static_cast<std::uint16_t>(std::stoi(argv[2]));
    }
    std::string port_str = std::to_string(port);

    Scenario scenario;
    std::string err;
    if (!parse_scenario_file(scenario_path, scenario, &err)) {
        std::cerr << "[launcher] invalid scenario: " << err << '\n';
        return 1;
    }

    std::vector<pid_t> active_pids;

    // 1. Spawn Race Server (note: sync CLI args with Teammate A if server takes a port arg)
    std::vector<std::string> server_args = {"./server"};
    if (port != DEFAULT_PORT) {
        server_args.push_back(port_str);
    }
    pid_t server_pid = spawn_process(server_args);
    if (server_pid < 0) {
        return 1;
    }
    active_pids.push_back(server_pid);
    std::cout << "[launcher] started server (PID=" << server_pid << ")\n";

    // Brief wait for server bind/listen; controller's connect_tcp also retries cleanly
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    int early_status = 0;
    if (::waitpid(server_pid, &early_status, WNOHANG) == server_pid) {
        std::cerr << "[launcher] server exited prematurely\n";
        return 1;
    }

    // 2. Create Controller -> Launcher confirmation pipe and spawn Controller
    int ctrl_pipe[2] = {-1, -1};
    if (::pipe(ctrl_pipe) != 0) {
        std::perror("pipe failed");
        kill_and_reap(active_pids);
        return 1;
    }

    pid_t ctrl_pid = spawn_process(
        {"./controller", scenario_path, port_str, std::to_string(ctrl_pipe[1])}, ctrl_pipe[0]
    );
    ::close(ctrl_pipe[1]); // Parent only reads

    if (ctrl_pid < 0) {
        ::close(ctrl_pipe[0]);
        kill_and_reap(active_pids);
        return 1;
    }
    active_pids.push_back(ctrl_pid);
    std::cout << "[launcher] started controller (PID=" << ctrl_pid << ")\n";

    // 3. Wait for Controller to confirm SCENARIO_ACCEPTED
    std::string ctrl_reply;
    bool accepted = read_pipe_line(ctrl_pipe[0], ctrl_reply, 10000) && ctrl_reply == "ACCEPTED";
    ::close(ctrl_pipe[0]);

    if (!accepted) {
        std::cerr << "[launcher] controller failed or timed out (reply='" << ctrl_reply << "')\n";
        kill_and_reap(active_pids);
        return 1;
    }

    // 4. Spawn one Robot Client process per robot in scenario
    for (const auto &robot : scenario.robots) {
        pid_t rpid = spawn_process({"./robot", scenario_path, robot.id, port_str});
        if (rpid < 0) {
            kill_and_reap(active_pids);
            return 1;
        }
        active_pids.push_back(rpid);
        std::cout << "[launcher] started robot " << robot.id << " (PID=" << rpid << ")\n";
    }

    // 5. Reap all child processes with a watchdog timeout (30s)
    bool all_ok = true;
    auto watchdog = std::chrono::steady_clock::now() + std::chrono::seconds(30);

    while (!active_pids.empty()) {
        if (g_interrupted || std::chrono::steady_clock::now() >= watchdog) {
            std::cerr << "[launcher] " << (g_interrupted ? "interrupted" : "watchdog timeout")
                      << " — terminating remaining processes\n";
            kill_and_reap(active_pids);
            return 1;
        }

        int status = 0;
        pid_t done = ::waitpid(-1, &status, WNOHANG);
        if (done > 0) {
            active_pids.erase(
                std::remove(active_pids.begin(), active_pids.end(), done), active_pids.end()
            );
            if (WIFEXITED(status)) {
                int code = WEXITSTATUS(status);
                std::cout << "[launcher] PID=" << done << " exited with code " << code << '\n';
                if (code != 0) {
                    all_ok = false;
                }
            } else if (WIFSIGNALED(status)) {
                std::cout << "[launcher] PID=" << done << " killed by signal " << WTERMSIG(status)
                          << '\n';
                all_ok = false;
            }
        } else if (done == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        } else if (errno == ECHILD) {
            active_pids.clear();
        }
    }

    return all_ok ? 0 : 1;
}