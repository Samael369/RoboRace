# RoboRace Phase 1: Architecture, Team Task Breakdown & C++17 Guide

> **Note on Academic Integrity & Project Rules**:
> As agreed, **no AI code generation is used**. Every line of C++ code must be written by hand by you and your teammate. This document provides the engineering roadmap, system architecture, task distribution, and a comprehensive C++17 / POSIX concepts refresher to guide your implementation and debugging.

---

## 1. Project High-Level Architecture & Protocol Overview

In Phase 1 of **RoboRace**, the objective is to build a robust, multi-process distributed simulation framework on Linux using **C++ (C++17)** and **TCP sockets**.

### 1.1 The Five Main Components

```
                    +---------------------------+
                    |         LAUNCHER          |
                    | (forks & manages all procs|
                    +-------------+-------------+
                                  |
           +----------------------+----------------------+
           |                      |                      |
           v                      v                      v
    +--------------+      +---------------+      +---------------+
    |  CONTROLLER  |      | ROBOT CLIENT  |      | ROBOT CLIENT  |
    |  (PID: ...)  |      |  R1 (PID: ...) |     |  R2 (PID: ...) |
    +-------+------+      +-------+-------+      +-------+-------+
            |                     |                      |
            | (TCP localhost)     | (TCP localhost)      | (TCP localhost)
            |                     |                      |
            +------------------> [ RACE SERVER ] <-------+
                                  (PID: ...)
                                       |
                                       v
                              [ events.log ]
                                       |
                                       v
                              [ VERIFIER TOOL ]
                              -> verify_report.txt
```

1. **Launcher (`launcher`)**:
   - The master coordinator. Takes the `.race` scenario file path from CLI.
   - Spawns the Race Server child process (`fork()` + `execvp()`).
   - Waits for the server's TCP port to be open and accepting connections.
   - Spawns the Controller Client child process.
   - Spawns $N$ Robot Client child processes (one for each robot in the scenario).
   - Monitors all child processes using `waitpid()` to ensure clean termination with no zombies.
2. **Race Server (`server`)**:
   - Listens on a dedicated TCP port on `localhost`.
   - Manages multiple concurrent TCP connections (Controller + all Robots).
   - Validates JSON Lines messages and enforces the Phase 1 state machine:
     - `SCENARIO_UPLOAD` $\rightarrow$ `SCENARIO_ACCEPTED`
     - `REGISTER_ROBOT` $\rightarrow$ `REGISTERED`
     - `READY` $\rightarrow$ `READY_ACCEPTED`
     - Broadcast `START` once all robots defined in the scenario are ready.
     - `ACTION_REQUEST` $\rightarrow$ `STATE_UPDATE` (with `payload: {"status": "RECORDED"}`).
     - `CLIENT_DONE` $\rightarrow$ `DONE_ACCEPTED`
     - Broadcast `SESSION_FINISHED` and log `SESSION_END` once all robots finish.
   - Logs every event to `events.log` in atomic, exact format (`tick=-1 actor=... event=...`).
   - Handles edge cases: invalid JSON, missing fields, duplicate IDs, client disconnects (`EOF`), writing `CANCEL` events.
3. **Controller Client (`controller`)**:
   - Reads the `.race` JSON scenario file from disk.
   - Connects to the server over TCP.
   - Sends `SCENARIO_UPLOAD` with the full scenario content, awaits `SCENARIO_ACCEPTED`.
   - Waits for `START` and `SESSION_FINISHED`, then terminates cleanly.
4. **Robot Client (`robot`)**:
   - Executed with scenario path and robot identifier (e.g. `./robot demo01.race R1`).
   - Connects to the server over TCP.
   - Sequence: `REGISTER_ROBOT` $\rightarrow$ `READY` $\rightarrow$ wait for `START` $\rightarrow$ iterate through assigned actions (`ACTION_REQUEST` for `MOVE` or `OPEN:<chest>`), waiting for `STATE_UPDATE` after each $\rightarrow$ send `CLIENT_DONE` $\rightarrow$ wait for `SESSION_FINISHED` $\rightarrow$ close socket and exit.
5. **Verifier (`verifier`)**:
   - Standalone verification program.
   - Reads the `.race` scenario and `events.log`.
   - Evaluates the 5 mandatory checks:
     1. `unique_registration`: No duplicate robot registrations.
     2. `ready_before_start`: Every robot sent `READY` and received `READY_ACCEPTED` before `START`.
     3. `one_response_per_request`: Every request has exactly one matching response (or `CANCEL` if disconnected).
     4. `no_action_before_start`: No action requests or receives occurred before `START`.
     5. `clean_session_end`: Clean termination and `SESSION_END` logged.
   - Outputs `verify_report.txt` with status `PASS` or `FAIL`.

---

## 2. Team Task Distribution (Teammate A vs. Teammate B)

To ensure smooth parallel progress without merge conflicts or blocking dependencies, the workload is partitioned by system boundary:

```
+--------------------------------------------------------------------------------+
|                             SHARED CONTRACTS                                   |
|   - include/protocol.hpp (namespace roborace::protocol constants & codes)      |
|   - third_party/nlohmann/json.hpp (header-only nlohmann::json library)         |
+---------------------------------------+----------------------------------------+
|       TEAMMATE A: SERVER CORE         |      TEAMMATE B: CLIENTS & RUNNER      |
|                                       |                                        |
| 1. TCP Server & Socket Multiplexing   | 1. Scenario JSON Parser (.race reader) |
|    (socket, bind, listen, poll/thread)| 2. Robot Client (state machine & loop) |
| 2. TCP Stream Accumulator / Buffer    | 3. Controller Client (scenario upload) |
|    (handles sticky & fragmented pkts) | 4. Launcher (fork, execvp, waitpid)    |
| 3. Server State Machine & Validation  | 5. Verifier Tool & Report Generator    |
| 4. Thread-safe/Atomic events.log      | 6. Makefile & run_demo.sh script       |
| 5. Disconnect & CANCEL handling       | 7. Disconnect test scenario execution  |
+---------------------------------------+----------------------------------------+
```

### Detailed Breakdown

#### 👤 Teammate A: Server & Networking Engine
* **Task A1: TCP Accumulator & Framing (`net_buffer.cpp / .hpp`)**
  - Maintain a `std::string` read buffer per socket connection.
  - Read bytes from `recv()`, append them, and extract complete lines ending with `\n` (max `65536` bytes).
* **Task A2: TCP Server Socket Engine (`server_net.cpp / .hpp`)**
  - Create a listening socket with `SO_REUSEADDR`.
  - Use `poll()` or per-connection `std::thread`s (recommended for Phase 2 readiness) to manage concurrent client connections.
  - Track client socket descriptors, client roles, and IDs using STL containers (`std::unordered_map`, `std::vector`).
* **Task A3: Server State Machine & Message Handler (`server_logic.cpp`)**
  - Validate incoming JSON message schema (`type`, `race_id`, `sender`, `request_id`, `payload`).
  - Implement protocol stages:
    - Only accept one scenario upload from `CONTROLLER`.
    - Validate robot registrations against the scenario robot list; prevent duplicate IDs.
    - Barrier synchronization: wait until all robots send `READY` before broadcasting `START`.
    - Handle `ACTION_REQUEST`: reply with `STATE_UPDATE` (`{"status":"RECORDED"}`).
    - Collect `CLIENT_DONE` from all robots, then broadcast `SESSION_FINISHED`.
* **Task A4: Event Logger (`logger.cpp / .hpp`)**
  - Write atomic log lines to `events.log` via `std::ofstream` (protected by `std::mutex`) matching the exact format:
    `tick=-1 actor=<id> event=<name> resource=<res> result=<res> req=<id>`
  - Flush immediately (`file.flush()`) after every line.
* **Task A5: Error Handling & Disconnection Cleanup**
  - Detect socket close / EOF (`recv() == 0`).
  - For incomplete requests during disconnection, log `CANCEL` with `result=DISCONNECTED`.
  - Send appropriate `ERROR` messages (`INVALID_JSON`, `INVALID_MESSAGE`, `INVALID_STATE`, `DUPLICATE_ID`).

#### 👤 Teammate B: Clients, Launcher, Verifier & Infrastructure
* **Task B1: Scenario Parser (`scenario.cpp / .hpp`)**
  - Parse `.race` JSON into C++ structs (`ScenarioConfig`, `RobotConfig`, `ChestConfig`, `BridgeConfig`) using `std::string` and `std::vector`.
  - Validate all field types and constraints strictly, returning clear error messages on invalid scenarios.
* **Task B2: Robot Client (`robot_main.cpp`)**
  - Connect to server at `127.0.0.1:<port>`.
  - Send `REGISTER_ROBOT` $\rightarrow$ wait for `REGISTERED`.
  - Send `READY` $\rightarrow$ wait for `READY_ACCEPTED`.
  - Block until `START` broadcast is received.
  - Loop through actions: send `ACTION_REQUEST`, wait for `STATE_UPDATE`.
  - Send `CLIENT_DONE`, wait for `DONE_ACCEPTED`, wait for `SESSION_FINISHED`.
  - Cleanly close socket and `return 0`.
* **Task B3: Controller Client (`controller_main.cpp`)**
  - Read scenario file, wrap into `SCENARIO_UPLOAD`, send to server.
  - Wait for `SCENARIO_ACCEPTED`, `START`, and `SESSION_FINISHED`.
* **Task B4: Launcher Process (`launcher_main.cpp`)**
  - Take scenario path from `argv[1]`.
  - Parse scenario (or coordinate via pipe) to discover how many robots to spawn.
  - `fork()` and `execvp()` server, wait for port readiness.
  - `fork()` and `execvp()` controller.
  - `fork()` and `execvp()` each robot client (`./robot <scenario_path> <robot_id>`).
  - Loop with `waitpid(-1, &status, 0)` until all spawned children exit cleanly.
* **Task B5: Verifier (`verifier_main.cpp`)**
  - Parse `events.log` line-by-line using `std::ifstream` and `std::istringstream`.
  - Check the 5 required conditions and write `verify_report.txt`.
* **Task B6: Build & Test Automation (`Makefile`, `run_demo.sh`)**
  - Maintain the C++17 `Makefile` with clean compilation rules.
  - Write `run_demo.sh` to run the entire demo pipeline automatically.
  - Add disconnect test script (using `kill -KILL <robot_pid>`).

---

## 3. C++17 & POSIX Systems Programming Refresher

Here is a targeted guide to the C++17 idioms and POSIX syscalls used in this project.

### 3.1 Memory Management, RAII & STL Containers

Prefer standard C++ containers and RAII over manual `malloc`/`free` or raw `new`/`delete`. Memory and file descriptors clean themselves up when objects leave scope.

* **Standard Containers**:
  - `std::string`: dynamic text and byte buffers.
  - `std::vector<T>`: dynamic arrays (robot lists, actions, socket lists).
  - `std::unordered_map<K, V>` / `std::unordered_set<K>`: O(1) lookup for robot state and duplicate `request_id` detection.
  - `std::unique_ptr<T>`: single-owner heap objects when polymorphism or pointer stability is needed.

* **RAII File Descriptor Guard**:
  ```cpp
  #include <unistd.h>

  class UniqueFd {
  public:
      explicit UniqueFd(int fd = -1) : fd_(fd) {}
      ~UniqueFd() { reset(); }

      UniqueFd(const UniqueFd&) = delete;
      UniqueFd& operator=(const UniqueFd&) = delete;

      UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {}
      UniqueFd& operator=(UniqueFd&& other) noexcept {
          if (this != &other) {
              reset(other.release());
          }
          return *this;
      }

      int get() const { return fd_; }
      explicit operator bool() const { return fd_ >= 0; }

      int release() {
          int old = fd_;
          fd_ = -1;
          return old;
      }

      void reset(int new_fd = -1) {
          if (fd_ >= 0) {
              ::close(fd_);
          }
          fd_ = new_fd;
      }

  private:
      int fd_;
  };
  ```

### 3.2 Strings, Views & Formatting

Avoid `strcpy`, `sprintf`, and manual null-terminator bookkeeping.

* **`std::string` and `std::string_view`**:
  - Use `std::string_view` for read-only function parameters and constants (`protocol.hpp` defines constants as `inline constexpr std::string_view`).
  - Compare directly with `==`: `if (type == roborace::protocol::MSG_READY)`.
  - Build formatted log lines safely with `std::ostringstream`:
    ```cpp
    #include <sstream>
    #include <string>

    std::ostringstream oss;
    oss << "tick=" << tick
        << " actor=" << actor
        << " event=" << event
        << " resource=" << resource
        << " result=" << result
        << " req=" << req << '\n';
    std::string line = oss.str();
    ```

### 3.3 POSIX Process Management: `fork()`, `execvp()`, `waitpid()`

RoboRace requires every robot and controller to run in an independent process (`PID`).

* **`fork()` & `execvp()` in C++17**:
  In C++, string literals have type `const char[]`, while `execvp` takes `char* const argv[]`. Build arguments from `std::string` and pass `.data()` (available on non-const `std::string` since C++17) inside a `std::vector<char*>` to avoid `-Wwrite-strings` warnings:
  ```cpp
  #include <cstdlib>
  #include <iostream>
  #include <string>
  #include <sys/wait.h>
  #include <unistd.h>
  #include <vector>

  pid_t spawn_robot(const std::string& scenario_path, const std::string& robot_id) {
      pid_t pid = ::fork();
      if (pid < 0) {
          std::perror("fork failed");
          return -1;
      }
      if (pid == 0) {
          std::string prog = "./robot";
          std::string scen = scenario_path;
          std::string id = robot_id;
          std::vector<char*> argv = {prog.data(), scen.data(), id.data(), nullptr};
          ::execvp(argv[0], argv.data());
          std::perror("execvp failed");
          std::_Exit(EXIT_FAILURE); // Use _Exit in child after fork
      }
      return pid;
  }
  ```

* **`waitpid()`**: Harvest child process exit codes and prevent zombie processes:
  ```cpp
  int status = 0;
  pid_t finished_pid = 0;
  while ((finished_pid = ::waitpid(-1, &status, 0)) > 0) {
      if (WIFEXITED(status)) {
          std::cout << "Child " << finished_pid << " exited with status "
                    << WEXITSTATUS(status) << '\n';
      } else if (WIFSIGNALED(status)) {
          std::cout << "Child " << finished_pid << " killed by signal "
                    << WTERMSIG(status) << '\n';
      }
  }
  ```

### 3.4 POSIX TCP Sockets in C++

Network communication uses Linux TCP stream sockets (`<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`).

#### Server Setup:
```cpp
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);

int opt = 1;
::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

sockaddr_in address{}; // Value-initialization zeros all fields
address.sin_family = AF_INET;
address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
address.sin_port = htons(8080);

::bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
::listen(server_fd, 32);

sockaddr_in client_addr{};
socklen_t addrlen = sizeof(client_addr);
int client_fd = ::accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &addrlen);
```

#### Client Setup:
```cpp
int sock_fd = ::socket(AF_INET, SOCK_STREAM, 0);

sockaddr_in serv_addr{};
serv_addr.sin_family = AF_INET;
serv_addr.sin_port = htons(8080);
::inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

::connect(sock_fd, reinterpret_cast<sockaddr*>(&serv_addr), sizeof(serv_addr));
```

#### Complete Send (`send_all`) & Receiving:
A single `send()` is not guaranteed to write all bytes. Always loop until everything is sent and retry on `EINTR`:
```cpp
#include <array>
#include <cerrno>
#include <string_view>

bool send_all(int fd, std::string_view data) {
    std::size_t total_sent = 0;
    while (total_sent < data.size()) {
        ssize_t n = ::send(fd, data.data() + total_sent, data.size() - total_sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        total_sent += static_cast<std::size_t>(n);
    }
    return true;
}

// Receiving into a fixed stack array:
std::array<char, 4096> recv_buf{};
ssize_t bytes_read = ::recv(sock_fd, recv_buf.data(), recv_buf.size(), 0);
if (bytes_read == 0) {
    // Peer disconnected gracefully (EOF)
} else if (bytes_read < 0) {
    // Handle EINTR / EAGAIN / error
}
```

### 3.5 The "Golden Rule" of TCP Stream Sockets: Line Framing

> ⚠️ **CRITICAL OS CONCEPT**:
> TCP is a continuous byte stream, **not** a packet/message protocol.
> If a client sends `{"type":"A"}\n` and `{"type":"B"}\n`, a single `recv()` might return:
> - Just `{"type":"A"` (fragmented packet)
> - `{"type":"A"}\n{"type":"B"}\n` (coalesced/sticky packets)
> - Or `A"}\n{"type":"B"` across multiple calls.

**How to handle this in C++**:
Keep a `std::string buffer_` per connection:
1. Append newly received bytes: `buffer_.append(recv_buf.data(), static_cast<std::size_t>(bytes_read));`
2. Check if `buffer_` exceeds `MAX_LINE_LENGTH` (`65536`) without containing a `'\n'`. If so, trigger a framing error and close the connection.
3. Loop while `auto pos = buffer_.find('\n'); pos != std::string::npos`:
   - Extract the line: `std::string line = buffer_.substr(0, pos);` (strip trailing `'\r'` if any).
   - Remove the consumed line and newline: `buffer_.erase(0, pos + 1);`
   - Validate line length (`<= MAX_LINE_LENGTH`) and parse JSON.

### 3.6 Signal Handling & Crucial POSIX Quirks

* **`SIGPIPE`**:
  Calling `send()` on a socket whose peer has already closed raises `SIGPIPE`, which terminates the process by default. Ignore it at the start of `main()` (and/or pass `MSG_NOSIGNAL` to `send()`):
  ```cpp
  #include <csignal>

  std::signal(SIGPIPE, SIG_IGN);
  ```
  Once ignored, `send()` returns `-1` with `errno == EPIPE`, letting your code handle disconnects cleanly.

### 3.7 JSON Handling in C++ (Using `nlohmann/json`)

We vendor the single-header **`nlohmann/json`** library in `third_party/nlohmann/json.hpp` (included with `-isystem third_party`, so include it as `#include <nlohmann/json.hpp>`).

* **Parsing JSON Lines without exceptions crashing your loop**:
  Pass `allow_exceptions = false` to `nlohmann::json::parse` and check `is_discarded()`:
  ```cpp
  #include <nlohmann/json.hpp>
  #include "protocol.hpp"

  using json = nlohmann::json;

  json msg = json::parse(line, nullptr, false);
  if (msg.is_discarded() || !msg.is_object()) {
      // Invalid JSON syntax or non-object JSON -> reply ERR_INVALID_JSON
      return;
  }
  ```

* **Strict field & type checking**:
  Beware that in JSON/C++, `is_number()` accepts floats! Always use exact checks:
  - String field: `msg.contains("type") && msg["type"].is_string()`
  - Strict integer (not float, not bool): `val.is_number_integer() && !val.is_boolean()`
  - Object field: `msg.contains("payload") && msg["payload"].is_object()`

  Example envelope access:
  ```cpp
  if (!msg.contains("type") || !msg["type"].is_string()) {
      // Missing or invalid type -> ERR_INVALID_MESSAGE
  }
  std::string msg_type = msg["type"].get<std::string>();
  ```

* **Constructing and serializing a compact JSON Line**:
  ```cpp
  json resp = {
      {"type", roborace::protocol::MSG_STATE_UPDATE},
      {"race_id", "demo01"},
      {"sender", roborace::protocol::ACTOR_SERVER},
      {"reply_to", "R1-03"},
      {"payload", {{"status", roborace::protocol::STATUS_RECORDED}}}
  };

  // dump() with no indent argument produces compact single-line JSON
  std::string wire = resp.dump() + '\n';
  send_all(client_fd, wire);
  ```

### 3.8 File I/O for `events.log`

Writing to `events.log` must be thread-safe (guard with `std::mutex`) and flushed immediately so crashes never drop recent events:
```cpp
#include <fstream>
#include <mutex>

std::ofstream log_file("events.log");
// Inside a mutex-protected logger method:
log_file << "tick=-1 actor=" << actor
         << " event=" << event
         << " resource=" << resource
         << " result=" << result
         << " req=" << req << '\n';
log_file.flush();
```

---

## 4. Suggested Project Directory Structure

Organize the repository cleanly:

```
RoboRace/
├── Makefile
├── README.md
├── run_demo.sh
├── .clang-format
├── instructions/             # Project specs, pdf, contracts, scenarios
│   ├── CONTRACT_FA.md
│   ├── README_FA.md
│   ├── RoboRace_Phase1.pdf
│   ├── examples/
│   └── scenarios/
├── third_party/              # Vendored third-party libraries
│   └── nlohmann/
│       └── json.hpp          # Single-header nlohmann/json (v3.11.3)
├── include/                  # Project header files
│   ├── protocol.hpp          # Shared protocol definitions & constants
│   ├── net_buffer.hpp        # TCP stream line accumulator
│   ├── scenario.hpp          # Scenario config structs & parser API
│   └── logger.hpp            # Thread-safe events.log writer
├── src/                      # C++17 source files
│   ├── common/
│   │   ├── net_buffer.cpp
│   │   └── scenario.cpp
│   ├── server/
│   │   ├── server_main.cpp
│   │   ├── server_logic.cpp
│   │   └── logger.cpp
│   ├── client/
│   │   ├── robot_main.cpp
│   │   └── controller_main.cpp
│   ├── launcher/
│   │   └── launcher_main.cpp
│   └── verifier/
│       └── verifier_main.cpp
└── bin/                      # Compiled executables (git-ignored)
```

---

## 5. Step-by-Step Implementation Roadmap

Work incrementally in 6 milestones:

| Phase | Objective | Teammate A | Teammate B |
|---|---|---|---|
| **Step 1** | Setup & Parsing | Set up C++17 `Makefile`, `protocol.hpp`, `nlohmann/json.hpp` | Implement `scenario.cpp` to parse & validate `.race` files |
| **Step 2** | Sockets & Accumulator | Build TCP server + `std::string` line accumulator (`net_buffer.cpp`) | Build basic TCP client connection + `send_all` helper |
| **Step 3** | Registration Handshake | Server handles `SCENARIO_UPLOAD`, `REGISTER_ROBOT`, `READY`, logs to `events.log` | Controller uploads scenario; Robot registers & sends `READY` |
| **Step 4** | Barrier & Actions Loop | Server broadcasts `START`, receives `ACTION_REQUEST`, replies `STATE_UPDATE` | Robots wait for `START`, iterate actions, send `CLIENT_DONE` |
| **Step 5** | Launcher & Session End | Server receives all `CLIENT_DONE`, broadcasts `SESSION_FINISHED`, logs `SESSION_END` | Implement `launcher` with `fork()`, `execvp()`, `waitpid()` |
| **Step 6** | Verifier & Edge Cases | Handle EOF/Disconnects, write `CANCEL` logs | Implement `verifier`, test with `demo01`, `stress10`, `disconnect_demo` |

---

## 6. How to Test Each Scenario

1. **Normal Run (`demo01.race`)**:
   ```bash
   make
   ./run_demo.sh instructions/scenarios/demo01.race
   ./verifier instructions/scenarios/demo01.race events.log --phase=1
   ```
   *Expected*: All 5 checks in `verify_report.txt` output `result=PASS`.

2. **10 Concurrent Clients (`stress10.race`)**:
   - Tests server multiplexing / concurrency under 10 simultaneous robot processes.
   - Verifies no race conditions or dropped TCP packets during registration barrier.

3. **Client Disconnection Test (`disconnect_demo.race`)**:
   - Run the simulation in the background.
   - Find the PID of robot R1 (`pgrep -f "robot.*R1"`).
   - Send `kill -KILL <PID>`.
   - The server must detect the socket closure (`recv() == 0`), log `DISCONNECT` and `CANCEL` for any pending requests, abort the session cleanly, and exit without hanging or crashing.
