CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -pedantic -g -Iinclude
LDFLAGS ?= -lpthread -lm
DEPFLAGS = -MMD -MP

OBJDIR = obj
BINDIR = bin

# Core targets required by project spec
TARGETS = server controller robot launcher verifier

COMMON_SRCS = src/common/cJSON.c
COMMON_OBJS = $(patsubst src/%.c, $(OBJDIR)/%.o, $(COMMON_SRCS))

ALL_C_SRCS = $(wildcard src/*/*.c)
ALL_HEADERS = $(wildcard include/*.h)

.PHONY: all clean format check-format dirs help

all: dirs $(TARGETS)

dirs:
	@mkdir -p $(OBJDIR)/common $(OBJDIR)/server $(OBJDIR)/client $(OBJDIR)/launcher $(OBJDIR)/verifier $(BINDIR)

# Common object compilation
$(OBJDIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Target binaries
server: $(OBJDIR)/server/server_main.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@cp -f $@ $(BINDIR)/$@

controller: $(OBJDIR)/client/controller_main.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@cp -f $@ $(BINDIR)/$@

robot: $(OBJDIR)/client/robot_main.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@cp -f $@ $(BINDIR)/$@

launcher: $(OBJDIR)/launcher/launcher_main.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@cp -f $@ $(BINDIR)/$@

verifier: $(OBJDIR)/verifier/verifier_main.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@cp -f $@ $(BINDIR)/$@

# Include compiler-generated dependency files
-include $(wildcard $(OBJDIR)/*/*.d)

# Code formatting targets using clang-format
format:
	@echo "Formatting C source and header files..."
	@clang-format -i $(ALL_C_SRCS) $(ALL_HEADERS)
	@echo "Code formatting complete."

check-format:
	@echo "Checking formatting compliance..."
	@clang-format --dry-run --Werror $(ALL_C_SRCS) $(ALL_HEADERS)
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
