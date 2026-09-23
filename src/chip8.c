#include "chip8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

void chip8_cycle(chip8_t *c8)
{
    uint16_t opcode;
    uint8_t x;
    uint8_t y;
    uint8_t n;
    uint8_t nn;
    uint16_t nnn;

    if (c8 == NULL) {
        return;
    }

    /* CHIP-8 opcodes are two-byte big-endian values. */
    opcode = (uint16_t)((c8->memory[c8->pc] << 8) |
                        c8->memory[c8->pc + 1U]);
    c8->pc += 2U;

    /* The masks isolate the X/Y registers, nibble, byte, and address fields. */
    x = (uint8_t)((opcode & 0x0F00U) >> 8);
    y = (uint8_t)((opcode & 0x00F0U) >> 4);
    n = (uint8_t)(opcode & 0x000FU);
    nn = (uint8_t)(opcode & 0x00FFU);
    nnn = opcode & 0x0FFFU;

    /* The high nibble selects the primary opcode family. */
    switch (opcode & 0xF000U) {
    case 0x0000U:
        if (opcode == 0x00E0U) {
            memset(c8->gfx, 0, sizeof(c8->gfx));
            c8->draw_flag = true;
        } else if (opcode == 0x00EEU) {
            c8->sp--;
            c8->pc = c8->stack[c8->sp];
        }
        break;
    case 0x1000U:
        c8->pc = nnn;
        break;
    case 0x2000U:
        c8->stack[c8->sp++] = c8->pc;
        c8->pc = nnn;
        break;
    case 0x3000U:
        if (c8->V[x] == nn) {
            c8->pc += 2U;
        }
        break;
    case 0x4000U:
        if (c8->V[x] != nn) {
            c8->pc += 2U;
        }
        break;
    case 0x5000U:
        if (n == 0U && c8->V[x] == c8->V[y]) {
            c8->pc += 2U;
        }
        break;
    case 0x6000U:
        c8->V[x] = nn;
        break;
    case 0x7000U:
        c8->V[x] += nn;
        break;
    case 0x8000U:
        switch (n) {
        case 0x0U:
            c8->V[x] = c8->V[y];
            break;
        case 0x1U:
            c8->V[x] |= c8->V[y];
            break;
        case 0x2U:
            c8->V[x] &= c8->V[y];
            break;
        case 0x3U:
            c8->V[x] ^= c8->V[y];
            break;
        case 0x4U: {
            /* Keep the carry in a 16-bit sum before truncating Vx to 8 bits. */
            uint16_t sum = (uint16_t)c8->V[x] + (uint16_t)c8->V[y];
            c8->V[x] = (uint8_t)(sum & 0xFFU);
            c8->V[0xF] = (sum > 255U) ? 1U : 0U;
            break;
        }
        case 0x5U: {
            /* VF reports no borrow; equality counts as a successful subtraction. */
            uint8_t flag = (c8->V[x] >= c8->V[y]) ? 1U : 0U;
            c8->V[x] = c8->V[x] - c8->V[y];
            c8->V[0xF] = flag;
            break;
        }
        case 0x6U: {
            /* Shift Vx in place; VF receives the discarded low bit. */
            uint8_t flag = c8->V[x] & 0x1U;
            c8->V[x] >>= 1;
            c8->V[0xF] = flag;
            break;
        }
        case 0x7U: {
            /* Reverse subtraction uses the same no-borrow convention as 8XY5. */
            uint8_t flag = (c8->V[y] >= c8->V[x]) ? 1U : 0U;
            c8->V[x] = c8->V[y] - c8->V[x];
            c8->V[0xF] = flag;
            break;
        }
        case 0xEU: {
            /* Shift Vx in place; VF receives the discarded high bit. */
            uint8_t flag = (uint8_t)((c8->V[x] >> 7) & 0x1U);
            c8->V[x] <<= 1;
            c8->V[0xF] = flag;
            break;
        }
        default:
            break;
        }
        break;
    case 0x9000U:
        if (n == 0U && c8->V[x] != c8->V[y]) {
            c8->pc += 2U;
        }
        break;
    case 0xA000U:
        c8->I = nnn;
        break;
    case 0xB000U:
        c8->pc = nnn + c8->V[0];
        break;
    case 0xC000U:
        c8->V[x] = (uint8_t)((rand() % 256) & nn);
        break;
    case 0xD000U: {
        uint8_t vx = c8->V[x];
        uint8_t vy = c8->V[y];
        uint8_t row;

        /* XOR set sprite bits; VF records whether any lit pixel was erased. */
        c8->V[0xF] = 0;
        for (row = 0; row < n; row++) {
            uint8_t pixel_byte = c8->memory[c8->I + row];
            uint16_t y_coord = (uint16_t)((vy + row) % CHIP8_SCREEN_HEIGHT);
            uint8_t col;

            for (col = 0; col < 8U; col++) {
                uint16_t x_coord = (uint16_t)((vx + col) %
                                              CHIP8_SCREEN_WIDTH);

                if ((pixel_byte & (uint8_t)(0x80U >> col)) != 0U) {
                    uint32_t index = x_coord +
                                     (y_coord * CHIP8_SCREEN_WIDTH);

                    if (c8->gfx[index] == 1U) {
                        c8->V[0xF] = 1;
                    }
                    c8->gfx[index] ^= 1U;
                }
            }
        }
        c8->draw_flag = true;
        break;
    }
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
        case 0x07U:
            c8->V[x] = c8->delay_timer;
            break;
        case 0x0AU: {
            uint8_t key;
            bool key_pressed = false;

            for (key = 0; key < CHIP8_KEY_COUNT; key++) {
                if (c8->keypad[key]) {
                    c8->V[x] = key;
                    key_pressed = true;
                    break;
                }
            }
            if (!key_pressed) {
                c8->pc -= 2U;
            }
            break;
        }
        case 0x15U:
            c8->delay_timer = c8->V[x];
            break;
        case 0x18U:
            c8->sound_timer = c8->V[x];
            break;
        case 0x1EU:
            c8->I += c8->V[x];
            break;
        case 0x29U:
            c8->I = CHIP8_FONT_ADDRESS + ((c8->V[x] & 0x0FU) * 5U);
            break;
        case 0x33U:
            c8->memory[c8->I] = c8->V[x] / 100U;
            c8->memory[c8->I + 1U] = (c8->V[x] / 10U) % 10U;
            c8->memory[c8->I + 2U] = c8->V[x] % 10U;
            break;
        case 0x55U: {
            uint8_t i;

            for (i = 0; i <= x; i++) {
                c8->memory[c8->I + i] = c8->V[i];
            }
            break;
        }
        case 0x65U: {
            uint8_t i;

            for (i = 0; i <= x; i++) {
                c8->V[i] = c8->memory[c8->I + i];
            }
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