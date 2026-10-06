CC       = gcc
CPPFLAGS = -Iinclude
CFLAGS   = -Wall -Wextra -Wpedantic -Werror -std=c11 -O2

TARGET  = chip8
SOURCES = $(wildcard src/*.c)
OBJECTS = $(SOURCES:src/%.c=build/%.o)

TEST_TARGET = build/test_opcodes
TEST_SRC    = tests/test_opcodes.c src/chip8.c

# OS Detection and SDL2 Configuration
ifeq ($(OS),Windows_NT)
    TARGET_EXT = .exe
    # Fallback to local deps/SDL2 if system-wide doesn't exist
    SDL2_DIR    ?= deps/SDL2
    SDL2_CFLAGS ?= -I$(SDL2_DIR)/include/SDL2
    SDL2_LIBS   ?= -L$(SDL2_DIR)/lib -lmingw32 -lSDL2main -lSDL2
    
    # Windows cmd.exe compatible commands
    MKDIR_BUILD = if not exist build mkdir build
    CLEAN_CMD   = rmdir /s /q build 2>nul & del /q $(TARGET)$(TARGET_EXT) 2>nul
    TEST_RUNNER = $(TEST_TARGET)$(TARGET_EXT)
else
    TARGET_EXT =
    # Use sdl2-config dynamically on POSIX
    SDL2_CFLAGS ?= $(shell sdl2-config --cflags)
    SDL2_LIBS   ?= $(shell sdl2-config --libs)
    
    # POSIX shell commands
    MKDIR_BUILD = mkdir -p build
    CLEAN_CMD   = rm -rf build $(TARGET)
    TEST_RUNNER = ./$(TEST_TARGET)
endif

.PHONY: all debug test clean

all: $(TARGET)$(TARGET_EXT)

$(TARGET)$(TARGET_EXT): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJECTS) $(SDL2_LIBS) -o $@

build/%.o: src/%.c include/chip8.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL2_CFLAGS) -c $< -o $@

# Order-only prerequisite: create the build directory once.
build:
	@$(MKDIR_BUILD)

# ---------------------------------------------------------------------------
# make test - build the standalone test runner and execute it.
# ---------------------------------------------------------------------------
$(TEST_TARGET)$(TARGET_EXT): $(TEST_SRC) include/chip8.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_SRC) -o $@

test: $(TEST_TARGET)$(TARGET_EXT)
	$(TEST_RUNNER)

debug: CFLAGS += -g -fsanitize=address,undefined
debug: LDFLAGS += -fsanitize=address,undefined
debug: clean all

clean:
	-@$(CLEAN_CMD)