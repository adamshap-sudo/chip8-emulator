# CHIP-8 CPU Emulator & Architecture Study

A highly robust, C11-compliant CHIP-8 emulator engineered with a focus on defensive programming, architectural correctness, and memory safety. Designed as an exercise in low-level CPU emulation and vulnerability mitigation, this project cleanly models the fetch-decode-execute cycle of a classic virtual machine while aggressively sandboxing all memory transactions to prevent common C-based vulnerabilities.

## 🧠 Architectural Overview

At its core, this emulator faithfully implements a Harvard-like architecture with a single flat 4KB memory array addressing both instructions and data.

### The Fetch-Decode-Execute Cycle
The core loop execution (`chip8_cycle`) is heavily structured:
- **Fetch:** The Program Counter (PC) loads two consecutive bytes from memory (Big-Endian configuration).
- **Decode:** Bitwise masking operations efficiently extract opcodes, registers (X, Y), and address fields (NNN) from the 16-bit instruction without branching overhead. 
- **Execute:** A highly optimized `switch` dispatch maps opcodes directly to ALU operations, branching logic, memory accesses, and I/O.

### Configurable Quirks & Sub-Architecture
The emulator accounts for subtle architectural differences between the original COSMAC VIP CHIP-8 interpreter and the modern SUPER-CHIP/CHIP-48 variations, particularly concerning shift behaviors (`8XY6`, `8XYE`) and memory increments during `FX55/FX65` (via `CHIP8_CONFIG_SHIFT_USE_VY` and `CHIP8_CONFIG_INCREMENT_I` directives).

## 🛡️ Memory Safety & Vulnerability Mitigation

When writing emulators in C, handling guest CPU state operations carelessly can often bleed into host-process memory vulnerabilities. This codebase employs strict sandboxing and bounds-checking to guarantee safe execution regardless of ROM integrity.

* **Out-of-Bounds (OOB) Protection:**
  - Instructions like `FX55`, `FX65`, and `FX33` (BCD conversion) aggressively bounds-check the Index register (`I`) before any read/write operations. 
  - Without these guards, an attacker could load a malicious ROM that sets `I` past the 4KB boundary, resulting in an OOB Write which can be chained into Arbitrary Code Execution (ACE) on the host system.
  - Sprite drawing (`DXYN`) bounds-checks row fetching against memory capacity, preventing OOB Reads that could otherwise leak sensitive host memory layout data.
* **Stack Underflow & Overflow Guards:**
  - Missing stack checks are classical attack vectors. Pushing beyond the CHIP-8 stack limit of 16 (CALLs) or popping below 0 (RETs) will cause silent halt execution rather than a host-level buffer overflow or underflow (mitigating Return-Oriented Programming (ROP) exploit paths).
* **Defensive Compilation:**
  - Build checks strictly adhere to modern safety standards using `-Wall -Wextra -Wpedantic -Werror -std=c11`, ensuring that zero undefined behavior (UB) slips into compilation.

## 🚀 Building & Running

### Requirements
- GCC (MinGW for Windows)
- GNU Make
- SDL2 (Included locally for zero-hassle building)

### Build Instructions

```bash
# Clean previous builds
make clean

# Build the executable
make all
```

### Running ROMs
You can execute the emulator by supplying a CHIP-8 ROM as the first argument:
```bash
./chip8.exe "roms/IBM Logo.ch8"
./chip8.exe "roms/Tetris.ch8"
```
*(Public domain ROMs such as IBM Logo, Pong, Tetris, and Space Invaders are included in the `roms/` directory).*

## 📖 Source Code Breakdown

- **`include/chip8.h`**: Memory layout, core state structures, and configurable quirks. Struct layout is designed carefully to keep the `memory` array positioned first for deterministic mitigation and cache locality.
- **`src/chip8.c`**: CPU mechanics, opcode routing, fetch-decode-execute logic, and safety guards.
- **`src/main.c`**: Host interfacing — maps SDL2 events to the emulator's keypad array and translates the CHIP-8 graphical buffer to RGBA textures at 60 Hz.

## 📄 License
This project is licensed under the MIT License - see the `LICENSE` file for details.
