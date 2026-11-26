CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -g -Iinclude -isystem third_party
LDFLAGS ?= -pthread
DEPFLAGS = -MMD -MP

OBJDIR = obj
BINDIR = bin

# Core targets required by project spec
TARGETS = server controller robot launcher verifier

COMMON_SRCS = $(wildcard src/common/*.cpp)
COMMON_OBJS = $(patsubst src/%.cpp, $(OBJDIR)/%.o, $(COMMON_SRCS))

SERVER_SRCS = $(sort $(wildcard src/server/*.cpp) src/server/server_main.cpp)
SERVER_OBJS = $(patsubst src/%.cpp, $(OBJDIR)/%.o, $(SERVER_SRCS))

LAUNCHER_SRCS = $(sort $(wildcard src/launcher/*.cpp) src/launcher/launcher_main.cpp)
LAUNCHER_OBJS = $(patsubst src/%.cpp, $(OBJDIR)/%.o, $(LAUNCHER_SRCS))

VERIFIER_SRCS = $(sort $(wildcard src/verifier/*.cpp) src/verifier/verifier_main.cpp)
VERIFIER_OBJS = $(patsubst src/%.cpp, $(OBJDIR)/%.o, $(VERIFIER_SRCS))

ALL_CPP_SRCS = $(wildcard src/*/*.cpp)
ALL_HEADERS = $(wildcard include/*.hpp)

.PHONY: all clean format check-format dirs help

all: dirs $(TARGETS)

dirs:
	@mkdir -p $(OBJDIR)/common $(OBJDIR)/server $(OBJDIR)/client $(OBJDIR)/launcher $(OBJDIR)/verifier $(BINDIR)

# Pattern rule for C++ object compilation
$(OBJDIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

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
	@echo "Formatting C++ source and header files..."
	@clang-format -i $(ALL_CPP_SRCS) $(ALL_HEADERS)
	@echo "Code formatting complete."

check-format:
	@echo "Checking formatting compliance..."
	@clang-format --dry-run --Werror $(ALL_CPP_SRCS) $(ALL_HEADERS)
	@echo "All files conform to formatting rules."

clean:
	@echo "Cleaning build artifacts and logs..."
	@rm -rf $(OBJDIR) $(BINDIR) $(TARGETS) events.log verify_report.txt
	@echo "Clean complete."

help:
	@echo "Available make targets:"
	@echo "  all          : Build all executables (server, controller, robot, launcher, verifier)"
	@echo "  format       : Auto-format all .cpp and .hpp files using clang-format"
	@echo "  check-format : Verify code formatting without modifying files"
	@echo "  clean        : Remove object files, binaries, and generated logs"
