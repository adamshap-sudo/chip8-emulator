#include "chip8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Fontset definition (declared extern in chip8.h).
 * Each of the 16 hex glyphs is five bytes tall; all 16 are stored at 0x050.
 * ------------------------------------------------------------------------- */
const uint8_t chip8_fontset[80] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0, /* 0 */
    0x20, 0x60, 0x20, 0x20, 0x70, /* 1 */
    0xF0, 0x10, 0xF0, 0x80, 0xF0, /* 2 */
    0xF0, 0x10, 0xF0, 0x10, 0xF0, /* 3 */
    0x90, 0x90, 0xF0, 0x10, 0x10, /* 4 */
    0xF0, 0x80, 0xF0, 0x10, 0xF0, /* 5 */
    0xF0, 0x80, 0xF0, 0x90, 0xF0, /* 6 */
    0xF0, 0x10, 0x20, 0x40, 0x40, /* 7 */
    0xF0, 0x90, 0xF0, 0x90, 0xF0, /* 8 */
    0xF0, 0x90, 0xF0, 0x10, 0xF0, /* 9 */
    0xF0, 0x90, 0xF0, 0x90, 0x90, /* A */
    0xE0, 0x90, 0xE0, 0x90, 0xE0, /* B */
    0xF0, 0x80, 0x80, 0x80, 0xF0, /* C */
    0xE0, 0x90, 0x90, 0x90, 0xE0, /* D */
    0xF0, 0x80, 0xF0, 0x80, 0xF0, /* E */
    0xF0, 0x80, 0xF0, 0x80, 0x80  /* F */
};

/* ---------------------------------------------------------------------------
 * chip8_init – zero the state, set the PC to the program start address, and
 * copy the fontset into the reserved region at 0x050.
 * ------------------------------------------------------------------------- */
void chip8_init(chip8_t *c8)
{
    if (c8 == NULL) {
        return;
    }

    memset(c8, 0, sizeof(*c8));
    c8->pc = CHIP8_PROGRAM_ADDRESS;
    memcpy(&c8->memory[CHIP8_FONT_ADDRESS],
           chip8_fontset,
           sizeof(chip8_fontset));
}

/* ---------------------------------------------------------------------------
 * chip8_load_rom – open a ROM file and copy it into memory starting at 0x200.
 * Returns false if the ROM is missing, unreadable, or too large to fit.
 * ------------------------------------------------------------------------- */
bool chip8_load_rom(chip8_t *c8, const char *path)
{
    FILE *file;
    long file_size;
    size_t bytes_read;
    const size_t available_memory = CHIP8_MEMORY_SIZE - CHIP8_PROGRAM_ADDRESS;

    if (c8 == NULL || path == NULL) {
        return false;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        return false;
    }

    file_size = ftell(file);
    if (file_size < 0L || (unsigned long)file_size > available_memory) {
        fclose(file);
        return false;
    }

    if (fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    bytes_read = fread(&c8->memory[CHIP8_PROGRAM_ADDRESS],
                       1U,
                       (size_t)file_size,
                       file);
    if (fclose(file) != 0 || bytes_read != (size_t)file_size) {
        return false;
    }

    return true;
}

/* ---------------------------------------------------------------------------
 * chip8_cycle – fetch, decode, and execute a single CHIP-8 instruction.
 *
 * Safety guarantees enforced here:
 *   • PC is clamped so that both opcode bytes always fall inside memory.
 *   • Stack pointer is bounds-checked before every push (CALL) and pop (RET).
 *   • Every memory write by FX33, FX55, FX65 is guarded against I overflow.
 *   • Every sprite-row read by DXYN is guarded against I + row overflow.
 * ------------------------------------------------------------------------- */
void chip8_cycle(chip8_t *c8)
{
    uint16_t opcode;
    uint8_t  x;
    uint8_t  y;
    uint8_t  n;
    uint8_t  nn;
    uint16_t nnn;

    if (c8 == NULL) {
        return;
    }

    /* --- Fetch ----------------------------------------------------------- */
    /* Guard: both bytes of the opcode must lie within the 4 KB memory array. */
    if (c8->pc >= CHIP8_MEMORY_SIZE - 1U) {
        return; /* PC out-of-bounds – halt silently. */
    }

    /* CHIP-8 opcodes are two-byte big-endian values. */
    opcode = (uint16_t)(((uint16_t)c8->memory[c8->pc] << 8) |
                        (uint16_t)c8->memory[c8->pc + 1U]);
    c8->pc += 2U;

    /* --- Decode ---------------------------------------------------------- */
    /* The masks isolate the X/Y registers, nibble, byte, and address fields.
     * INTERVIEW NOTE: Bitwise masking extracts specific parts of the 16-bit opcode.
     * For example, opcode & 0x0FFF zeros out the highest 4 bits, extracting the 12-bit address (nnn).
     * Similarly, (opcode & 0x0F00) >> 8 extracts the 4 bits for register X, shifting them to the lowest 4 bits.
     */
    x   = (uint8_t)((opcode & 0x0F00U) >> 8);
    y   = (uint8_t)((opcode & 0x00F0U) >> 4);
    n   = (uint8_t)(opcode  & 0x000FU);
    nn  = (uint8_t)(opcode  & 0x00FFU);
    nnn = opcode & 0x0FFFU;

    /* --- Execute --------------------------------------------------------- */
    /* The high nibble selects the primary opcode family. */
    switch (opcode & 0xF000U) {

    /* 0x00E0 – CLS: clear the display. */
    /* 0x00EE – RET: return from subroutine. */
    case 0x0000U:
        if (opcode == 0x00E0U) {
            memset(c8->gfx, 0, sizeof(c8->gfx));
            c8->draw_flag = true;
        } else if (opcode == 0x00EEU) {
            /* BUG FIX: guard against stack underflow (sp == 0).
             * INTERVIEW NOTE: Missing this check would cause an integer underflow on sp (e.g., 0 - 1 = 255),
             * leading to an Out-of-Bounds (OOB) memory read on c8->stack[255]. In a real C application,
             * this could be leveraged for Return-Oriented Programming (ROP) or control-flow hijacking.
             */
            if (c8->sp == 0U) {
                return; /* Stack underflow – halt silently. */
            }
            c8->sp--;
            c8->pc = c8->stack[c8->sp];
        }
        break;

    /* 0x1NNN – JP addr */
    case 0x1000U:
        c8->pc = nnn;
        break;

    /* 0x2NNN – CALL addr */
    case 0x2000U:
        /* BUG FIX: guard against stack overflow (sp already at capacity).
         * INTERVIEW NOTE: Without this check, a deep nesting of CALLs would cause an Out-of-Bounds (OOB) write
         * past the end of the stack array. In a vulnerable C program, this stack overflow could overwrite
         * adjacent data or return addresses, leading to Arbitrary Code Execution (ACE).
         */
        if (c8->sp >= CHIP8_STACK_SIZE) {
            return; /* Stack overflow – halt silently. */
        }
        c8->stack[c8->sp] = c8->pc;
        c8->sp++;
        c8->pc = nnn;
        break;

    /* 0x3XNN – SE Vx, byte */
    case 0x3000U:
        if (c8->V[x] == nn) {
            c8->pc += 2U;
        }
        break;

    /* 0x4XNN – SNE Vx, byte */
    case 0x4000U:
        if (c8->V[x] != nn) {
            c8->pc += 2U;
        }
        break;

    /* 0x5XY0 – SE Vx, Vy */
    case 0x5000U:
        if (n == 0U && c8->V[x] == c8->V[y]) {
            c8->pc += 2U;
        }
        break;

    /* 0x6XNN – LD Vx, byte */
    case 0x6000U:
        c8->V[x] = nn;
        break;

    /* 0x7XNN – ADD Vx, byte  (no flag update) */
    case 0x7000U:
        c8->V[x] += nn;
        break;

    /* 0x8XYN – arithmetic / logic */
    case 0x8000U:
        switch (n) {

        /* 0x8XY0 – LD  Vx, Vy */
        case 0x0U:
            c8->V[x] = c8->V[y];
            break;

        /* 0x8XY1 – OR  Vx, Vy */
        case 0x1U:
            c8->V[x] |= c8->V[y];
            break;

        /* 0x8XY2 – AND Vx, Vy */
        case 0x2U:
            c8->V[x] &= c8->V[y];
            break;

        /* 0x8XY3 – XOR Vx, Vy */
        case 0x3U:
            c8->V[x] ^= c8->V[y];
            break;

        /* 0x8XY4 – ADD Vx, Vy
         *
         * The carry flag is computed from the 16-bit sum BEFORE truncating
         * Vx to 8 bits.  The flag is written AFTER Vx, so even when X == 0xF
         * VF ends up holding the carry (not the result of the addition).
         *
         * Correct order: Vx = low byte of sum, VF = carry.
         */
        case 0x4U: {
            uint16_t sum = (uint16_t)c8->V[x] + (uint16_t)c8->V[y];
            uint8_t  carry = (sum > 0xFFU) ? 1U : 0U;
            c8->V[x]    = (uint8_t)(sum & 0xFFU);
            c8->V[0xF]  = carry;           /* always written last */
            break;
        }

        /* 0x8XY5 – SUB Vx, Vy  (Vx = Vx - Vy)
         *
         * VF = 1 when Vx >= Vy (no borrow); VF = 0 when Vx < Vy (borrow).
         * Flag written after Vx for the X == 0xF edge-case.
         */
        case 0x5U: {
            uint8_t no_borrow = (c8->V[x] >= c8->V[y]) ? 1U : 0U;
            c8->V[x]   = c8->V[x] - c8->V[y];
            c8->V[0xF] = no_borrow;        /* always written last */
            break;
        }

        /* 0x8XY6 – SHR Vx {, Vy}
         *
         * CHIP8_CONFIG_SHIFT_USE_VY == 1 (COSMAC VIP):
         *   Vx = Vy >> 1; VF = old LSB of Vy.
         * CHIP8_CONFIG_SHIFT_USE_VY == 0 (modern default):
         *   Vx >>= 1;     VF = old LSB of Vx.
         *
         * VF is always written after Vx.
         */
        case 0x6U: {
#if CHIP8_CONFIG_SHIFT_USE_VY
            uint8_t flag = c8->V[y] & 0x1U;
            c8->V[x]   = c8->V[y] >> 1;
#else
            uint8_t flag = c8->V[x] & 0x1U;
            c8->V[x] >>= 1;
#endif
            c8->V[0xF] = flag;             /* always written last */
            break;
        }

        /* 0x8XY7 – SUBN Vx, Vy  (Vx = Vy - Vx)
         *
         * VF = 1 when Vy >= Vx (no borrow); VF = 0 when Vy < Vx (borrow).
         * Flag written after Vx for the X == 0xF edge-case.
         */
        case 0x7U: {
            uint8_t no_borrow = (c8->V[y] >= c8->V[x]) ? 1U : 0U;
            c8->V[x]   = c8->V[y] - c8->V[x];
            c8->V[0xF] = no_borrow;        /* always written last */
            break;
        }

        /* 0x8XYE – SHL Vx {, Vy}
         *
         * CHIP8_CONFIG_SHIFT_USE_VY == 1 (COSMAC VIP):
         *   Vx = Vy << 1; VF = old MSB of Vy.
         * CHIP8_CONFIG_SHIFT_USE_VY == 0 (modern default):
         *   Vx <<= 1;     VF = old MSB of Vx.
         *
         * VF is always written after Vx.
         */
        case 0xEU: {
#if CHIP8_CONFIG_SHIFT_USE_VY
            uint8_t flag = (c8->V[y] >> 7) & 0x1U;
            c8->V[x]   = (uint8_t)(c8->V[y] << 1);
#else
            uint8_t flag = (c8->V[x] >> 7) & 0x1U;
            c8->V[x]   = (uint8_t)(c8->V[x] << 1);
#endif
            c8->V[0xF] = flag;             /* always written last */
            break;
        }

        default:
            break;
        }
        break;

    /* 0x9XY0 – SNE Vx, Vy */
    case 0x9000U:
        if (n == 0U && c8->V[x] != c8->V[y]) {
            c8->pc += 2U;
        }
        break;

    /* 0xANNN – LD I, addr */
    case 0xA000U:
        c8->I = nnn;
        break;

    /* 0xBNNN – JP V0, addr */
    case 0xB000U:
        c8->pc = nnn + (uint16_t)c8->V[0];
        break;

    /* 0xCXNN – RND Vx, byte */
    case 0xC000U:
        c8->V[x] = (uint8_t)((uint8_t)(rand() & 0xFF) & nn);
        break;

    /* 0xDXYN – DRW Vx, Vy, nibble
     *
     * XOR each set sprite bit onto the display buffer wrapping at screen
     * edges.  VF is set to 1 if any previously-lit pixel is turned off
     * (collision), and reset to 0 before drawing begins.
     *
     * BUG FIX: sprite-row memory access is now guarded (I + row < 4096).
     */
    case 0xD000U: {
        uint8_t  vx  = c8->V[x];
        uint8_t  vy  = c8->V[y];
        uint8_t  row;

        c8->V[0xF] = 0U;

        for (row = 0U; row < n; row++) {
            uint8_t  pixel_byte;
            uint16_t y_coord;
            uint8_t  col;

            /* BUG FIX: guard sprite-row read against I + row >= 4096.
             * INTERVIEW NOTE: Failing to bounds-check I + row could allow the emulator to read memory
             * outside the 4KB RAM buffer. This constitutes an Out-of-Bounds (OOB) Read, which can be
             * exploited to leak sensitive information from the host process memory.
             */
            if ((uint32_t)c8->I + row >= CHIP8_MEMORY_SIZE) {
                break;
            }

            pixel_byte = c8->memory[c8->I + row];
            y_coord    = (uint16_t)((vy + row) % CHIP8_SCREEN_HEIGHT);

            for (col = 0U; col < 8U; col++) {
                uint16_t x_coord;
                uint32_t index;

                if ((pixel_byte & (uint8_t)(0x80U >> col)) == 0U) {
                    continue; /* Sprite bit is clear – nothing to do. */
                }

                x_coord = (uint16_t)((vx + col) % CHIP8_SCREEN_WIDTH);
                index   = (uint32_t)x_coord +
                          (uint32_t)(y_coord * CHIP8_SCREEN_WIDTH);

                if (c8->gfx[index] != 0U) {
                    c8->V[0xF] = 1U; /* Collision detected. */
                }
                c8->gfx[index] ^= 1U;
            }
        }

        c8->draw_flag = true;
        break;
    }

    /* 0xEX9E – SKP  Vx  (skip if key Vx pressed) */
    /* 0xEXA1 – SKNP Vx  (skip if key Vx not pressed) */
    case 0xE000U:
        if (nn == 0x9EU && c8->V[x] < CHIP8_KEY_COUNT &&
            c8->keypad[c8->V[x]]) {
            c8->pc += 2U;
        } else if (nn == 0xA1U && c8->V[x] < CHIP8_KEY_COUNT &&
                   !c8->keypad[c8->V[x]]) {
            c8->pc += 2U;
        }
        break;

    case 0xF000U:
        switch (nn) {

        /* 0xFX07 – LD Vx, DT */
        case 0x07U:
            c8->V[x] = c8->delay_timer;
            break;

        /* 0xFX0A – LD Vx, K  (block until a key is pressed) */
        case 0x0AU: {
            uint8_t key;
            bool key_pressed = false;

            for (key = 0U; key < CHIP8_KEY_COUNT; key++) {
                if (c8->keypad[key]) {
                    c8->V[x]     = key;
                    key_pressed  = true;
                    break;
                }
            }
            if (!key_pressed) {
                c8->pc -= 2U; /* Re-execute this instruction next cycle. */
            }
            break;
        }

        /* 0xFX15 – LD DT, Vx */
        case 0x15U:
            c8->delay_timer = c8->V[x];
            break;

        /* 0xFX18 – LD ST, Vx */
        case 0x18U:
            c8->sound_timer = c8->V[x];
            break;

        /* 0xFX1E – ADD I, Vx */
        case 0x1EU:
            c8->I += c8->V[x];
            break;

        /* 0xFX29 – LD F, Vx  (set I to the font sprite for digit Vx & 0xF) */
        case 0x29U:
            c8->I = CHIP8_FONT_ADDRESS + ((uint16_t)(c8->V[x] & 0x0FU) * 5U);
            break;

        /* 0xFX33 – LD B, Vx  (store BCD of Vx at I, I+1, I+2)
         *
         * BUG FIX: all three target addresses are guarded before writing.
         * Integer division on uint8_t has well-defined, UB-free semantics
         * for all values 0-255.
         * INTERVIEW NOTE: If we did not check I + 2 >= 4096, an attacker could set I to 4095 and
         * cause an Out-of-Bounds Write. OOB writes are critical vulnerabilities that can corrupt
         * adjacent memory structures and eventually lead to Arbitrary Code Execution (ACE).
         */
        case 0x33U:
            if (c8->I + 2U >= CHIP8_MEMORY_SIZE) {
                break; /* Not enough room – skip silently. */
            }
            c8->memory[c8->I]      = c8->V[x] / 100U;
            c8->memory[c8->I + 1U] = (c8->V[x] / 10U) % 10U;
            c8->memory[c8->I + 2U] = c8->V[x] % 10U;
            break;

        /* 0xFX55 – LD [I], Vx  (store V0-Vx into memory starting at I)
         *
         * BUG FIX: every target address is guarded before writing.
         *
         * Quirk (CHIP8_CONFIG_INCREMENT_I):
         *   0 – I is left unchanged (CHIP-48 / SUPER-CHIP / modern default).
         *   1 – I is incremented by X + 1 (original COSMAC VIP behaviour).
         */
        case 0x55U: {
            uint8_t i;

            for (i = 0U; i <= x; i++) {
                if ((uint32_t)c8->I + i >= CHIP8_MEMORY_SIZE) {
                    break; /* Out-of-bounds – stop early. */
                }
                c8->memory[c8->I + i] = c8->V[i];
            }
#if CHIP8_CONFIG_INCREMENT_I
            c8->I += (uint16_t)(x + 1U);
#endif
            break;
        }

        /* 0xFX65 – LD Vx, [I]  (read V0-Vx from memory starting at I)
         *
         * BUG FIX: every source address is guarded before reading.
         *
         * Quirk (CHIP8_CONFIG_INCREMENT_I):
         *   0 – I is left unchanged (CHIP-48 / SUPER-CHIP / modern default).
         *   1 – I is incremented by X + 1 (original COSMAC VIP behaviour).
         */
        case 0x65U: {
            uint8_t i;

            for (i = 0U; i <= x; i++) {
                if ((uint32_t)c8->I + i >= CHIP8_MEMORY_SIZE) {
                    break; /* Out-of-bounds – stop early. */
                }
                c8->V[i] = c8->memory[c8->I + i];
            }
#if CHIP8_CONFIG_INCREMENT_I
            c8->I += (uint16_t)(x + 1U);
#endif
            break;
        }

        default:
            break;
        }
        break;

    default:
        break;
    }
}

/* ---------------------------------------------------------------------------
 * chip8_update_timers – decrement both countdown timers toward zero at 60 Hz.
 * ------------------------------------------------------------------------- */
void chip8_update_timers(chip8_t *c8)
{
    if (c8 == NULL) {
        return;
    }

    if (c8->delay_timer > 0U) {
        c8->delay_timer--;
    }
    if (c8->sound_timer > 0U) {
        c8->sound_timer--;
    }
}