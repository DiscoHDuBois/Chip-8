/*
Frontend grafico SDL2: finestra ridimensionabile, tastiera con pressione e rilascio veri,
beep a onda quadra. Viene compilato solo se SDL2 è installata (vedi Makefile).
*/
#include <stdio.h>
#include <SDL.h>

#include "frontend.h"

#define SCALE       10           // pixel della finestra per ogni pixel CHIP-8 (all'avvio)
#define COLOR_ON    0xFFFFFFFF   // ARGB
#define COLOR_OFF   0xFF000000
#define SAMPLE_RATE 44100
#define BEEP_HZ     440

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;         // 64x32: è la scheda video a ingrandirla fino alla finestra
static SDL_AudioDeviceID audio;  // 0 = niente audio, si va avanti in silenzio
static Uint64 next_frame;        // istante (performance counter) in cui inizia il prossimo frame

// Stesso layout del frontend terminale. Gli scancode indicano la posizione fisica
// del tasto, quindi la griglia 4x4 resta tale anche su tastiere non QWERTY
// Tastierino CHIP-8   ->  tastiera PC
//   1 2 3 C               1 2 3 4
//   4 5 6 D               q w e r
//   7 8 9 E               a s d f
//   A 0 B F               z x c v
static const SDL_Scancode keymap[0x10] = {
    SDL_SCANCODE_X, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3,
    SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_A,
    SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_Z, SDL_SCANCODE_C,
    SDL_SCANCODE_4, SDL_SCANCODE_R, SDL_SCANCODE_F, SDL_SCANCODE_V
};

// SDL chiama questa funzione da un altro thread ogni volta che la scheda audio vuole campioni.
// Onda quadra: metà periodo sopra lo zero (128 nel formato U8), metà sotto
static void
audio_callback(void *userdata, Uint8 *stream, int len) {
    static unsigned phase;
    const unsigned half = SAMPLE_RATE / BEEP_HZ / 2;
    (void) userdata;
    for (int s = 0; s < len; s++, phase++)
        stream[s] = (phase / half) % 2 ? 128 + 24 : 128 - 24;
}

static int
sdl2_close(const chip8 *vm) {
    (void) vm;
    if (audio) SDL_CloseAudioDevice(audio);
    if (tex)   SDL_DestroyTexture(tex);
    if (ren)   SDL_DestroyRenderer(ren);
    if (win)   SDL_DestroyWindow(win);
    SDL_Quit();
    return 1;
}

static int
sdl2_init(const frontend_config *cfg) {
    (void) cfg;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) goto fail;
    win = SDL_CreateWindow("CHIP-8", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           SCREEN_W * SCALE, SCREEN_H * SCALE, SDL_WINDOW_RESIZABLE);
    if (win == NULL) goto fail;
    ren = SDL_CreateRenderer(win, -1, 0);
    if (ren == NULL) goto fail;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");    // pixel netti, non sfumati
    SDL_RenderSetLogicalSize(ren, SCREEN_W, SCREEN_H);        // proporzioni 2:1 anche ridimensionando
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                            SCREEN_W, SCREEN_H);
    if (tex == NULL) goto fail;

    // L'audio è facoltativo: se non c'è una scheda audio l'emulatore funziona lo stesso
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec want = { 0 };
        want.freq = SAMPLE_RATE;
        want.format = AUDIO_U8;
        want.channels = 1;
        want.samples = 512;
        want.callback = audio_callback;
        audio = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);  // parte in pausa
    }
    if (audio == 0) fprintf(stderr, "SDL: audio non disponibile (%s)\n", SDL_GetError());

    next_frame = SDL_GetPerformanceCounter();
    return 1;

fail:
    fprintf(stderr, "SDL: %s\n", SDL_GetError());
    sdl2_close(NULL);
    return 0;
}

static int
sdl2_poll(chip8 *vm) {                     // ESC o chiusura della finestra -> ritorna 0
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) return 0;
        if (ev.type != SDL_KEYDOWN && ev.type != SDL_KEYUP) continue;
        if (ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE) return 0;
        if (ev.key.repeat) continue;       // la ripetizione automatica non è una nuova pressione
        for (int k = 0; k < 0x10; k++)
            if (keymap[k] == ev.key.keysym.scancode) {
                vm->keys[k] = ev.type == SDL_KEYDOWN;
                if (ev.type == SDL_KEYDOWN) vm->key_event = k;
            }
    }
    return 1;
}

static void
sdl2_beep(int on) {
    if (audio) SDL_PauseAudioDevice(audio, !on);
}

static void
sdl2_draw(const chip8 *vm) {
    Uint32 pixels[SCREEN_H * SCREEN_W];
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++)
            pixels[y * SCREEN_W + x] = vm->screen[y][x] ? COLOR_ON : COLOR_OFF;

    SDL_UpdateTexture(tex, NULL, pixels, SCREEN_W * sizeof pixels[0]);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);

    // 60 Hz: si aspetta fino a una scadenza fissa invece di dormire sempre 1/60 s,
    // così il tempo speso a disegnare non rallenta l'emulazione
    Uint64 freq = SDL_GetPerformanceFrequency(), now = SDL_GetPerformanceCounter();
    next_frame += freq / 60;
    if (next_frame > now) SDL_Delay((Uint32) ((next_frame - now) * 1000 / freq));
    else                  next_frame = now;    // in ritardo: si riparte da adesso, senza rincorrere
}

const frontend frontend_sdl2 = {
    "sdl", "finestra grafica SDL2 con audio",
    sdl2_init, sdl2_poll, sdl2_beep, sdl2_draw, sdl2_close
};
