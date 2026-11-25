# RoboRace Phase 1: Architecture, Team Task Breakdown & C++17 Systems Guide

> **Note on Academic Integrity & Project Rules**:
> As agreed, **no AI code generation is used**. Every line of C++ code must be written by hand by you and your teammate. This document provides the engineering roadmap, system architecture, task distribution, and a comprehensive C++17 / POSIX concepts refresher to guide your implementation and debugging.

---

## 1. Project High-Level Architecture & Protocol Overview

In Phase 1 of **RoboRace**, the objective is to build a robust, multi-process distributed simulation framework on Linux using **C++17** and **POSIX TCP sockets**. 

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
|   - protocol.h (Message structs, JSON parser wrappers, constants, error codes)  |
|   - cJSON library integration                                                  |
+---------------------------------------+----------------------------------------+
|       TEAMMATE A: SERVER CORE         |      TEAMMATE B: CLIENTS & RUNNER      |
|                                       |                                        |
| 1. TCP Server & Socket Multiplexing   | 1. Scenario JSON Parser (.race reader) |
|    (socket, bind, listen, poll/select)| 2. Robot Client (state machine & loop) |
| 2. TCP Stream Accumulator / Buffer    | 3. Controller Client (scenario upload) |
|    (handles sticky & fragmented pkts) | 4. Launcher (fork, execvp, waitpid)    |
| 3. Server State Machine & Validation  | 5. Verifier Tool & Report Generator    |
| 4. Thread-safe/Atomic events.log      | 6. Makefile & run_demo.sh script       |
| 5. Disconnect & CANCEL handling       | 7. Disconnect test scenario execution  |
+---------------------------------------+----------------------------------------+
```

### Detailed Breakdown

#### 👤 Teammate A: Server & Networking Engine
* **Task A1: TCP Accumulator & Framing (`net_buffer.c / .h`)**
  - Implement a dynamic buffer per socket connection.
  - Read bytes from `recv()`, accumulate them, extract full lines ending with `\n` (max 65536 bytes).
* **Task A2: TCP Server Socket Engine (`server_net.c / .h`)**
  - Create listening socket with `SO_REUSEADDR`.
  - Use `poll()` or `select()` (or POSIX threads) to manage multiple concurrent client connections.
  - Track client socket descriptors, client types, and IDs.
* **Task A3: Server State Machine & Message Handler (`server_logic.c`)**
  - Validate incoming JSON message schema (`type`, `race_id`, `sender`, `request_id`, `payload`).
  - Implement protocol stages:
    - Only accept one scenario upload from `CONTROLLER`.
    - Validate robot registrations against scenario robot list; prevent duplicate IDs.
    - Barrier synchronization: Wait until all robots send `READY` before broadcasting `START`.
    - Handle `ACTION_REQUEST`: reply with `STATE_UPDATE` (`{"status":"RECORDED"}`).
    - Collect `CLIENT_DONE` from all robots, then broadcast `SESSION_FINISHED`.
* **Task A4: Event Logger (`logger.c / .h`)**
  - Write atomic log lines to `events.log` matching the exact format:
    `tick=-1 actor=<id> event=<name> resource=<res> result=<res> req=<id>`
  - Ensure immediate file flush (`fflush(log_file)`).
* **Task A5: Error Handling & Disconnection Cleanup**
  - Detect socket close / EOF.
  - For incomplete requests during disconnection, log `CANCEL` with `result=DISCONNECTED`.
  - Send appropriate `ERROR` messages (`INVALID_JSON`, `INVALID_MESSAGE`, `INVALID_STATE`, `DUPLICATE_ID`).

#### 👤 Teammate B: Clients, Launcher, Verifier & Infrastructure
* **Task B1: Scenario Parser (`scenario.c / .h`)**
  - Read `.race` file into a C struct (e.g. `track_length`, `tick_ms`, list of robots and their `actions`).
  - Helper to get total expected robots and list of actions for a given robot ID.
* **Task B2: Robot Client (`robot_main.c`)**
  - Connect to server at `127.0.0.1:<port>`.
  - Send `REGISTER_ROBOT` $\rightarrow$ wait for `REGISTERED`.
  - Send `READY` $\rightarrow$ wait for `READY_ACCEPTED`.
  - Block until `START` broadcast is received.
  - Loop through actions: send `ACTION_REQUEST`, wait for `STATE_UPDATE`.
  - Send `CLIENT_DONE`, wait for `DONE_ACCEPTED`.
  - Cleanly close socket and exit(0).
* **Task B3: Controller Client (`controller_main.c`)**
  - Read scenario file, wrap into `SCENARIO_UPLOAD`, send to server.
  - Wait for `SCENARIO_ACCEPTED`, `START`, and `SESSION_FINISHED`.
* **Task B4: Launcher Process (`launcher_main.c`)**
  - Take scenario path from `argv[1]`.
  - Parse scenario or use a pipe to discover how many robots to spawn.
  - `fork()` and `execvp()` server, wait for port readiness (can poll connect or sleep briefly).
  - `fork()` and `execvp()` controller.
  - `fork()` and `execvp()` each robot client (`./robot <scenario_path> <robot_id>`).
  - Loop with `waitpid(-1, &status, 0)` until all spawned children exit cleanly.
* **Task B5: Verifier (`verifier_main.c` or Python/C script)**
  - Parse `events.log` line-by-line.
  - Check the 5 required conditions and write `verify_report.txt`.
* **Task B6: Build & Test Automation (`Makefile`, `run_demo.sh`)**
  - Write `Makefile` with clean compilation rules.
  - Write `run_demo.sh` to run the entire demo pipeline automatically.
  - Add disconnect test script (using `kill -KILL <robot_pid>`).

---

## 3. C Syntax & Systems Programming Refresher

Since you haven't written C since your first semester, here is a targeted guide to the exact concepts and idioms required for this project.

### 3.1 Memory Management & Pointers

In C, you manage your own memory. Every heap allocation must have a corresponding deallocation.

* **Dynamic Allocation**:
  ```c
  // Allocate memory for a string or buffer
  char *buffer = malloc(65536 * sizeof(char));
  if (buffer == NULL) {
      perror("malloc failed");
      exit(EXIT_FAILURE);
  }

  // Always free when done!
  free(buffer);
  buffer = NULL; // Best practice: avoid dangling pointer
  ```

* **Pointer Basics**:
  - `&x`: Address of variable `x`.
  - `*p`: Dereference pointer `p` (access the value at address `p`).
  - Struct pointer access: `client->socket_fd` is shorthand for `(*client).socket_fd`.

### 3.2 Safe String Operations

C strings are null-terminated arrays of characters (`'\0'`). Standard functions like `strcpy` or `sprintf` are unsafe because they can cause buffer overflows.

* **Safe formatting with `snprintf`**:
  ```c
  char message[512];
  // snprintf guarantees null-termination and prevents buffer overrun
  snprintf(message, sizeof(message), 
           "{\"type\":\"REGISTER_ROBOT\",\"sender\":\"%s\"}\n", robot_id);
  ```

* **Comparing and Searching**:
  - `strcmp(s1, s2) == 0`: strings are identical.
  - `strncmp(s1, s2, n) == 0`: first `n` characters match.
  - `strchr(buffer, '\n')`: finds the first occurrence of newline `\n`. Essential for packet framing!

### 3.3 POSIX Process Management: `fork()`, `execvp()`, `waitpid()`

RoboRace requires every robot and controller to run in an independent process (`PID`).

* **`fork()`**: Clones the current process. Returns:
  - `< 0`: Error.
  - `0`: In the child process.
  - `> 0`: In the parent process (returns child's PID).

* **`execvp()`**: Replaces the child process with a new program image.
  ```c
  pid_t pid = fork();
  if (pid < 0) {
      perror("fork failed");
  } else if (pid == 0) {
      // Child process: execute ./robot
      char *args[] = {"./robot", "scenarios/demo01.race", "R1", NULL};
      execvp(args[0], args);
      // If execvp returns, it failed!
      perror("execvp failed");
      exit(EXIT_FAILURE);
  } else {
      // Parent process (Launcher): keep track of child pid
      printf("Spawned child with PID: %d\n", pid);
  }
  ```

* **`waitpid()`**: Harvest child process exit codes and prevent "zombie" processes:
  ```c
  int status;
  pid_t finished_pid;
  // Wait for any child to terminate
  while ((finished_pid = waitpid(-1, &status, 0)) > 0) {
      if (WIFEXITED(status)) {
          printf("Child %d exited with status %d\n", finished_pid, WEXITSTATUS(status));
      } else if (WIFSIGNALED(status)) {
          printf("Child %d killed by signal %d\n", finished_pid, WTERMSIG(status));
      }
  }
  ```

### 3.4 Berkeley TCP Sockets

Network communication uses TCP stream sockets.

#### Server Setup:
```c
int server_fd = socket(AF_INET, SOCK_STREAM, 0);

// Allow immediate restart on the same port without TIME_WAIT errors:
int opt = 1;
setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

struct sockaddr_in address;
memset(&address, 0, sizeof(address));
address.sin_family = AF_INET;
address.sin_addr.s_addr = INADDR_ANY; // Or inet_addr("127.0.0.1")
address.sin_port = htons(8080);       // Host to Network Short

bind(server_fd, (struct sockaddr *)&address, sizeof(address));
listen(server_fd, 20); // Backlog of up to 20 connections

// Accept an incoming client connection:
struct sockaddr_in client_addr;
socklen_t addrlen = sizeof(client_addr);
int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
```

#### Client Setup:
```c
int sock_fd = socket(AF_INET, SOCK_STREAM, 0);

struct sockaddr_in serv_addr;
memset(&serv_addr, 0, sizeof(serv_addr));
serv_addr.sin_family = AF_INET;
serv_addr.sin_port = htons(8080);
inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

connect(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
```

#### Sending and Receiving Data:
```c
// Sending (must make sure all bytes are sent, or end with '\n')
ssize_t bytes_sent = send(sock_fd, msg, strlen(msg), 0);

// Receiving
char recv_buf[1024];
ssize_t bytes_read = recv(sock_fd, recv_buf, sizeof(recv_buf) - 1, 0);
if (bytes_read == 0) {
    // Client disconnected gracefully (EOF)
} else if (bytes_read < 0) {
    // Socket error
}
```

### 3.5 The "Golden Rule" of TCP Stream Sockets: Line Framing

> ⚠️ **CRITICAL OS CONCEPT**:
> TCP is a continuous byte stream, **not** a packet/message protocol.
> If the client sends two JSON messages:
> `{"type":"A"}\n` and `{"type":"B"}\n`
> A single `recv()` call on the server might return:
> - Just `{"type":"A"` (fragmented packet)
> - `{"type":"A"}\n{"type":"B"}\n` (coalesced/sticky packets)
> - Or `A"}\n{"type":"B"` across multiple calls.

**How to handle this in C**:
Maintain a per-connection read buffer (e.g. `char buf[65536]`, `int len`).
1. Append new bytes from `recv()` into `buf`.
2. Look for `\n` using `char *newline = memchr(buf, '\n', len);`.
3. If found:
   - Extract the line from `buf` to `newline`.
   - Null-terminate it (`*newline = '\0'`).
   - Process that complete JSON line.
   - Shift the remaining bytes (`len - (newline + 1 - buf)`) to the beginning of `buf` using `memmove()`.
4. If no `\n` is found, keep accumulating until the next `recv()`.

### 3.6 Signal Handling & Crucial POSIX Quirks

* **`SIGPIPE`**:
  If a process calls `send()` on a socket that the other side has already closed, the Linux kernel sends a `SIGPIPE` signal. By default, **`SIGPIPE` immediately terminates your entire process!**
  To prevent this, you **MUST** ignore `SIGPIPE` at the beginning of `main()` in both server and clients:
  ```c
  #include <signal.h>
  signal(SIGPIPE, SIG_IGN);
  ```
  Once ignored, `send()` will simply return `-1` with `errno == EPIPE`, which your code can gracefully handle.

### 3.7 JSON Handling in C (Using `cJSON`)

Since parsing JSON strings manually in C is tedious and error-prone, using an established lightweight single-file C library like **cJSON** (`cJSON.c` and `cJSON.h`) is standard:

* **Parsing JSON string**:
  ```c
  #include "cJSON.h"

  cJSON *json = cJSON_Parse(json_line);
  if (!json) {
      // Invalid JSON syntax!
      return ERROR_INVALID_JSON;
  }

  cJSON *type_item = cJSON_GetObjectItemCaseSensitive(json, "type");
  if (cJSON_IsString(type_item) && (type_item->valuestring != NULL)) {
      printf("Message type: %s\n", type_item->valuestring);
  }

  // Always delete the cJSON root to avoid memory leaks!
  cJSON_Delete(json);
  ```

* **Constructing JSON string**:
  ```c
  cJSON *resp = cJSON_CreateObject();
  cJSON_AddStringToObject(resp, "type", "STATE_UPDATE");
  cJSON_AddStringToObject(resp, "race_id", "demo01");
  cJSON_AddStringToObject(resp, "sender", "SERVER");
  cJSON_AddStringToObject(resp, "reply_to", "R1-03");

  cJSON *payload = cJSON_CreateObject();
  cJSON_AddStringToObject(payload, "status", "RECORDED");
  cJSON_AddItemToObject(resp, "payload", payload);

  // Print unformatted (single line without indentation)
  char *rendered = cJSON_PrintUnformatted(resp);
  
  // Send with newline
  send(client_fd, rendered, strlen(rendered), 0);
  send(client_fd, "\n", 1, 0);

  free(rendered);       // Free string allocated by cJSON
  cJSON_Delete(resp);   // Free JSON structure
  ```

### 3.8 File I/O for `events.log`

* Writing to `events.log` must be synchronous and reliable:
  ```c
  FILE *log_file = fopen("events.log", "w");
  if (!log_file) {
      perror("Failed to open events.log");
  }

  // Format: tick actor event resource result req
  fprintf(log_file, "tick=-1 actor=%s event=%s resource=%s result=%s req=%s\n",
          actor, event, resource, result, req);
  fflush(log_file); // CRUCIAL: flush buffer immediately so crashes don't lose logs!
  ```

---

## 4. Suggested Project Directory Structure

Organize your repository cleanly:

```
RoboRace/
├── Makefile
├── README.md
├── run_demo.sh
├── instructions/             # Project specs, pdf, contracts, scenarios
│   ├── CONTRACT_FA.md
│   ├── README_FA.md
│   ├── RoboRace_Phase1.pdf
│   ├── examples/
│   └── scenarios/
├── include/                  # Header files
│   ├── cJSON.h               # Third-party cJSON parser (C/C++ compatible)
│   ├── protocol.h            # Shared protocol definitions & constants
│   ├── net_buffer.hpp        # TCP stream accumulator (std::string-based)
│   ├── scenario.hpp          # Scenario parser structs
│   └── logger.hpp            # Log writer
├── src/                      # Source files
│   ├── common/
│   │   ├── cJSON.c           # Compiled as C11 by Makefile
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
│       └── verifier_main.cpp # (or verifier.py)
└── bin/                      # Compiled executables (git-ignored)
```

---

## 5. Step-by-Step Implementation Roadmap

Work incrementally in 6 milestones:

| Phase | Objective | Teammate A | Teammate B |
|---|---|---|---|
| **Step 1** | Setup & Parsing | Set up repo, `Makefile`, copy `cJSON.c` | Implement `scenario.cpp` to parse `.race` files |
| **Step 2** | Sockets & Accumulator | Build TCP server + `std::string` line accumulator | Build basic TCP client connection wrapper |
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
   - Tests server multiplexing under 10 simultaneous robot processes.
   - Verifies no race conditions or dropped TCP packets during registration barrier.

3. **Client Disconnection Test (`disconnect_demo.race`)**:
   - Run the simulation in the background.
   - Find the PID of robot R1 (`pgrep -f "robot.*R1"`).
   - Send `kill -KILL <PID>`.
   - The server must detect the socket closure (`recv() == 0`), log `DISCONNECT` and `CANCEL` for any pending requests, abort the session cleanly, and exit without hanging or crashing.
