CC = gcc
CPPFLAGS = -Iinclude
CFLAGS = -Wall -Wextra -Werror -pedantic -std=c99 -O2

SDL2_CFLAGS = $(shell sdl2-config --cflags)
SDL2_LIBS   = $(shell sdl2-config --libs)

TARGET = chip8
SOURCES = $(wildcard src/*.c)
OBJECTS = $(SOURCES:src/%.c=build/%.o)

.PHONY: all debug clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJECTS) $(SDL2_LIBS) -o $@

build/%.o: src/%.c include/chip8.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL2_CFLAGS) -c $< -o $@

debug: CFLAGS += -g -fsanitize=address,undefined
debug: LDFLAGS += -fsanitize=address,undefined
debug: clean $(TARGET)

clean:
	rm -rf build $(TARGET)