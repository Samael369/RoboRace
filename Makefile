CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -g -Iinclude
CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -pedantic -g -Iinclude
LDFLAGS ?= -lpthread -lm
DEPFLAGS = -MMD -MP

OBJDIR = obj
BINDIR = bin

# Core targets required by project spec
TARGETS = server controller robot launcher verifier

COMMON_C_SRCS = $(wildcard src/common/*.c)
COMMON_CPP_SRCS = $(wildcard src/common/*.cpp)
COMMON_OBJS = $(patsubst src/%.c, $(OBJDIR)/%.o, $(COMMON_C_SRCS)) \
              $(patsubst src/%.cpp, $(OBJDIR)/%.o, $(COMMON_CPP_SRCS))

SERVER_OBJS = $(patsubst src/%.cpp, $(OBJDIR)/%.o, $(wildcard src/server/*.cpp))
LAUNCHER_OBJS = $(patsubst src/%.cpp, $(OBJDIR)/%.o, $(wildcard src/launcher/*.cpp))
VERIFIER_OBJS = $(patsubst src/%.cpp, $(OBJDIR)/%.o, $(wildcard src/verifier/*.cpp))

ALL_SRCS = $(wildcard src/*/*.cpp src/*/*.c)
ALL_HEADERS = $(wildcard include/*.hpp include/*.h)

.PHONY: all clean format check-format dirs help

all: dirs $(TARGETS)

dirs:
	@mkdir -p $(OBJDIR)/common $(OBJDIR)/server $(OBJDIR)/client $(OBJDIR)/launcher $(OBJDIR)/verifier $(BINDIR)

# C++ object compilation
$(OBJDIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

# C object compilation (e.g., third-party cJSON.c)
$(OBJDIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Target binaries
server: $(SERVER_OBJS) $(COMMON_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)
	@cp -f $@ $(BINDIR)/$@

controller: $(OBJDIR)/client/controller_main.o $(COMMON_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)
	@cp -f $@ $(BINDIR)/$@

robot: $(OBJDIR)/client/robot_main.o $(COMMON_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)
	@cp -f $@ $(BINDIR)/$@

launcher: $(LAUNCHER_OBJS) $(COMMON_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)
	@cp -f $@ $(BINDIR)/$@

verifier: $(VERIFIER_OBJS) $(COMMON_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)
	@cp -f $@ $(BINDIR)/$@

# Include compiler-generated dependency files
-include $(wildcard $(OBJDIR)/*/*.d)

# Code formatting targets using clang-format
format:
	@echo "Formatting C/C++ source and header files..."
	@clang-format -i $(ALL_SRCS) $(ALL_HEADERS)
	@echo "Code formatting complete."

check-format:
	@echo "Checking formatting compliance..."
	@clang-format --dry-run --Werror $(ALL_SRCS) $(ALL_HEADERS)
	@echo "All files conform to formatting rules."

clean:
	@echo "Cleaning build artifacts and logs..."
	@rm -rf $(OBJDIR) $(BINDIR) $(TARGETS) events.log verify_report.txt
	@echo "Clean complete."

help:
	@echo "Available make targets:"
	@echo "  all          : Build all executables (server, controller, robot, launcher, verifier)"
	@echo "  format       : Auto-format all .c and .h files using clang-format"
	@echo "  check-format : Verify code formatting without modifying files"
	@echo "  clean        : Remove object files, binaries, and generated logs"
