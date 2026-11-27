# RoboRace - Operating Systems Course Project (Phase 1)

Welcome to the **RoboRace** OS Project workspace.

- Complete project instructions, contracts, and scenarios are located in the [instructions](./instructions/) directory.
- For the full architecture, team task distribution, implementation roadmap, and C++17 / POSIX concepts refresher, see:
  👉 **[PHASE1_ROADMAP_AND_GUIDE.md](./PHASE1_ROADMAP_AND_GUIDE.md)**

## Tech Stack & Build

- **Language:** C++17 (`g++`) on Linux (POSIX TCP sockets, `fork`/`execvp`/`waitpid`, `pthread`)
- **JSON Library:** Vendored `cJSON` (`include/cJSON.h`, `src/common/cJSON.c`)
- **Build & Format Commands:**
  - `make` — build `server`, `controller`, `robot`, `launcher`, and `verifier`
  - `make format` — format `src/` and `include/` with `clang-format`
  - `make check-format` — verify formatting without modifying files
  - `make clean` — remove object files, binaries, and generated logs
