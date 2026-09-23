#include "chip8.h"

#include <SDL.h>

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 320
#define FRAME_DURATION_MS 1000U / 60U
#define CPU_CYCLES_PER_FRAME 10U
#define AUDIO_SAMPLE_RATE 44100U
#define AUDIO_FREQUENCY 440U
#define AUDIO_BUFFER_SAMPLES 4410U
#define AUDIO_AMPLITUDE 3000

/* The phase accumulator advances each sample to approximate a 440 Hz tone. */
static void generate_square_wave(int16_t *buffer, size_t sample_count)
{
    size_t sample;
    uint32_t phase = 0U;

    for (sample = 0; sample < sample_count; sample++) {
        buffer[sample] = phase < AUDIO_SAMPLE_RATE / 2U
                             ? AUDIO_AMPLITUDE
                             : -AUDIO_AMPLITUDE;
        phase = (phase + AUDIO_FREQUENCY) % AUDIO_SAMPLE_RATE;
    }
}

static int keypad_index(SDL_Keycode keycode)
{
    switch (keycode) {
    case SDLK_1:
        return 0x1;
    case SDLK_2:
        return 0x2;
    case SDLK_3:
        return 0x3;
    case SDLK_4:
        return 0xC;
    case SDLK_q:
        return 0x4;
    case SDLK_w:
        return 0x5;
    case SDLK_e:
        return 0x6;
    case SDLK_r:
        return 0xD;
    case SDLK_a:
        return 0x7;
    case SDLK_s:
        return 0x8;
    case SDLK_d:
        return 0x9;
    case SDLK_f:
        return 0xE;
    case SDLK_z:
        return 0xA;
    case SDLK_x:
        return 0x0;
    case SDLK_c:
        return 0xB;
    case SDLK_v:
        return 0xF;
    default:
        return -1;
    }
}

int main(int argc, char **argv)
{
    chip8_t c8;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    SDL_AudioDeviceID audio_device = 0;
    SDL_AudioSpec audio_spec;
    int16_t audio_buffer[AUDIO_BUFFER_SAMPLES];
    SDL_Event event;
    uint32_t pixels[CHIP8_SCREEN_WIDTH * CHIP8_SCREEN_HEIGHT];
    bool running = true;
    int exit_code = 0;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <rom_path>\n", argv[0]);
        return 1;
    }

    chip8_init(&c8);
    if (!chip8_load_rom(&c8, argv[1])) {
        fprintf(stderr, "Failed to load ROM: %s\n", argv[1]);
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_zero(audio_spec);
    audio_spec.freq = AUDIO_SAMPLE_RATE;
    audio_spec.format = AUDIO_S16SYS;
    audio_spec.channels = 1;
    audio_spec.samples = 512;
    audio_device = SDL_OpenAudioDevice(NULL, 0, &audio_spec, NULL, 0);
    if (audio_device == 0) {
        fprintf(stderr, "SDL audio device creation failed: %s\n",
                SDL_GetError());
        exit_code = 1;
        goto cleanup;
    }
    generate_square_wave(audio_buffer, AUDIO_BUFFER_SAMPLES);

    window = SDL_CreateWindow("CHIP-8 Emulator",
                             SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED,
                             WINDOW_WIDTH,
                             WINDOW_HEIGHT,
                             SDL_WINDOW_SHOWN);
    if (window == NULL) {
        fprintf(stderr, "SDL window creation failed: %s\n", SDL_GetError());
        exit_code = 1;
        goto cleanup;
    }

    renderer = SDL_CreateRenderer(window, -1,
                                  SDL_RENDERER_ACCELERATED |
                                  SDL_RENDERER_PRESENTVSYNC);
    if (renderer == NULL) {
        fprintf(stderr, "SDL renderer creation failed: %s\n", SDL_GetError());
        exit_code = 1;
        goto cleanup;
    }

    texture = SDL_CreateTexture(renderer,
                                SDL_PIXELFORMAT_RGBA8888,
                                SDL_TEXTUREACCESS_STREAMING,
                                CHIP8_SCREEN_WIDTH,
                                CHIP8_SCREEN_HEIGHT);
    if (texture == NULL) {
        fprintf(stderr, "SDL texture creation failed: %s\n", SDL_GetError());
        exit_code = 1;
        goto cleanup;
    }

    while (running) {
        uint32_t frame_start = SDL_GetTicks();
        unsigned int cycle;

        while (SDL_PollEvent(&event) != 0) {
            int key;

            if (event.type == SDL_QUIT) {
                running = false;
                continue;
            }

            if (event.type != SDL_KEYDOWN && event.type != SDL_KEYUP) {
                continue;
            }

            if (event.type == SDL_KEYDOWN &&
                event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
                continue;
            }

            key = keypad_index(event.key.keysym.sym);
            if (key >= 0) {
                c8.keypad[key] = event.type == SDL_KEYDOWN;
            }
        }

        /* Ten CPU steps per 60 Hz frame decouple instructions from timer ticks. */
        for (cycle = 0; cycle < CPU_CYCLES_PER_FRAME; cycle++) {
            chip8_cycle(&c8);
        }
        chip8_update_timers(&c8);

        if (c8.sound_timer > 0U) {
            /* Keep one waveform buffer queued so playback stays continuous. */
            if (SDL_GetQueuedAudioSize(audio_device) <
                sizeof(audio_buffer)) {
                if (SDL_QueueAudio(audio_device,
                                   audio_buffer,
                                   sizeof(audio_buffer)) != 0) {
                    fprintf(stderr, "SDL audio queue failed: %s\n",
                            SDL_GetError());
                    exit_code = 1;
                    break;
                }
            }
            SDL_PauseAudioDevice(audio_device, 0);
        } else {
            /* Remove stale samples when the CHIP-8 sound timer expires. */
            SDL_ClearQueuedAudio(audio_device);
            SDL_PauseAudioDevice(audio_device, 1);
        }

        if (c8.draw_flag) {
            size_t pixel;

            for (pixel = 0; pixel < CHIP8_SCREEN_WIDTH * CHIP8_SCREEN_HEIGHT;
                 pixel++) {
                pixels[pixel] = c8.gfx[pixel] != 0U
                                    ? 0xFFFFFFFFU
                                    : 0x000000FFU;
            }

            if (SDL_UpdateTexture(texture,
                                  NULL,
                                  pixels,
                                  CHIP8_SCREEN_WIDTH * sizeof(uint32_t)) != 0) {
                fprintf(stderr, "SDL texture update failed: %s\n",
                        SDL_GetError());
                exit_code = 1;
                break;
            }
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);
            c8.draw_flag = false;
        }

        {
            uint32_t elapsed = SDL_GetTicks() - frame_start;

            if (elapsed < FRAME_DURATION_MS) {
                SDL_Delay(FRAME_DURATION_MS - elapsed);
            }
        }
    }

cleanup:
    if (audio_device != 0) {
        SDL_ClearQueuedAudio(audio_device);
        SDL_CloseAudioDevice(audio_device);
    }
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exit_code;
}