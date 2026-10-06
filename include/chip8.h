#ifndef CHIP8_H
#define CHIP8_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Architectural constants
 * ------------------------------------------------------------------------- */
#define CHIP8_MEMORY_SIZE    4096U
#define CHIP8_SCREEN_WIDTH     64U
#define CHIP8_SCREEN_HEIGHT    32U
#define CHIP8_STACK_SIZE       16U
#define CHIP8_KEY_COUNT        16U
#define CHIP8_FONT_ADDRESS   0x050U
#define CHIP8_PROGRAM_ADDRESS 0x200U

/* ---------------------------------------------------------------------------
 * Quirk / compatibility flags
 *
 * CHIP8_CONFIG_SHIFT_USE_VY
 *   0 (default / modern)  – 8XY6 and 8XYE shift Vx in-place (CHIP-48 /
 *                           SUPER-CHIP behaviour used by most modern ROMs).
 *   1 (original COSMAC VIP) – Vx = Vy >> 1  /  Vx = Vy << 1 before the
 *                           shift result is stored.  Required by a number of
 *                           classic ROMs (e.g. Blitz, Merlin).
 *
 * CHIP8_CONFIG_INCREMENT_I
 *   0 (default / modern)  – FX55 / FX65 leave I unchanged (CHIP-48 /
 *                           SUPER-CHIP behaviour).
 *   1 (original COSMAC VIP) – I is incremented by X + 1 after the loop,
 *                           matching the RCA 1802 reference interpreter.
 * ------------------------------------------------------------------------- */
#ifndef CHIP8_CONFIG_SHIFT_USE_VY
#  define CHIP8_CONFIG_SHIFT_USE_VY 0
#endif

#ifndef CHIP8_CONFIG_INCREMENT_I
#  define CHIP8_CONFIG_INCREMENT_I 0
#endif

/* ---------------------------------------------------------------------------
 * Core state structure
 * INTERVIEW NOTE: The order of members in this struct can affect memory padding and cache locality.
 * The 4KB memory array is deliberately placed first. In a vulnerable application, buffer overflows
 * often overwrite adjacent struct members. Placing memory buffers carefully is a mitigation tactic.
 * ------------------------------------------------------------------------- */
typedef struct {
    uint8_t  memory[CHIP8_MEMORY_SIZE];
    uint8_t  V[16];
    uint16_t I;
    uint16_t pc;
    uint16_t stack[CHIP8_STACK_SIZE];
    uint8_t  sp;
    uint8_t  delay_timer;
    uint8_t  sound_timer;
    /* One element per pixel; the renderer treats nonzero values as lit. */
    uint32_t gfx[CHIP8_SCREEN_WIDTH * CHIP8_SCREEN_HEIGHT];
    bool     keypad[CHIP8_KEY_COUNT];
    bool     draw_flag;
} chip8_t;

/* ---------------------------------------------------------------------------
 * Fontset – defined once in chip8.c; declared extern here so that including
 * this header in multiple translation units does not produce duplicate symbols.
 * Each hexadecimal glyph is five bytes tall; glyphs are loaded at 0x050.
 * ------------------------------------------------------------------------- */
extern const uint8_t chip8_fontset[80];

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */
void chip8_init(chip8_t *c8);
bool chip8_load_rom(chip8_t *c8, const char *path);
void chip8_cycle(chip8_t *c8);
void chip8_update_timers(chip8_t *c8);

#endif /* CHIP8_H */