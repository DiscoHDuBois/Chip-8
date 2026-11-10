#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>

#include "frontend.h"

#define KEY_HOLD_FRAMES  10      // il terminale non segnala il rilascio: tasto "premuto" per N frame

//Terminale: input non bloccante e output con sequenze ANSI
static struct termios term_orig;
static int term_saved, term_raw;
static int term_active;              // != 0: schermo pulito e cursore nascosto, da ripristinare

static void
term_restore(void) {                 // si può chiamare più volte (close, atexit, Ctrl+Z)
    if (!term_active) return;
    term_active = 0;
    if (term_raw) tcsetattr(STDIN_FILENO, TCSANOW, &term_orig);
    term_raw = 0;
    printf("\x1b[?25h\n");
    fflush(stdout);
}

static void
term_enter(void) {                   // raw mode, schermo pulito, cursore nascosto
    if (term_saved) {
        struct termios raw = term_orig;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        term_raw = tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0;
    }
    printf("\x1b[2J\x1b[?25l"); //per pulizia
    term_active = 1;
}

static volatile sig_atomic_t quit;     // != 0: numero del segnale di terminazione ricevuto
static volatile sig_atomic_t suspend;  // Ctrl+Z
static volatile sig_atomic_t resumed;  // ripresa dopo uno stop (fg/bg)

static void
on_signal(int sig) {
    if (sig == SIGTSTP)      suspend = 1;
    else if (sig == SIGCONT) resumed = 1;
    else                     quit = sig;
}

static void
sig_catch(int sig) {
    struct sigaction sa = { 0 };
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

//segnali
static void
term_suspend(void) {
    suspend = 0;
    term_restore();
    signal(SIGTSTP, SIG_DFL);
    raise(SIGTSTP);
    sig_catch(SIGTSTP);
    resumed = 1;
}

static int
terminal_init(const frontend_config *cfg) {
    static const int sigs[] = { SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGTSTP, SIGCONT };
    (void) cfg;
    atexit(term_restore);
    for (size_t s = 0; s < sizeof sigs / sizeof sigs[0]; s++) {
        struct sigaction old;
        if (sigaction(sigs[s], NULL, &old) == 0 && old.sa_handler == SIG_IGN) continue; // es. nohup
        sig_catch(sigs[s]);
    }
    term_saved = tcgetattr(STDIN_FILENO, &term_orig) == 0;
    term_enter();
    return 1;
}

/*
                  map
 Tastierino CHIP-8   ->  tastiera PC
   1 2 3 C               1 2 3 4
   4 5 6 D               q w e r
   7 8 9 E               a s d f
   A 0 B F               z x c v
*/
static const char keymap[0x10] = {
    'x', '1', '2', '3', 'q', 'w', 'e', 'a',
    's', 'd', 'z', 'c', '4', 'r', 'f', 'v'
};

static int
terminal_poll(chip8 *vm) {                 // ESC o segnale di terminazione -> 0
    char c;
    if (quit) return 0;
    if (suspend) term_suspend();
    if (resumed) { resumed = 0; term_enter(); }

    for (int k = 0; k < 0x10; k++) {
        if (vm->keys[k]) vm->keys[k]--;
    }
    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == 27) return 0; // <- qui ESC
        for (int k = 0; k < 0x10; k++)
            if (keymap[k] == tolower((unsigned char) c)) {
                vm->keys[k] = KEY_HOLD_FRAMES;
                vm->key_event = k;
            }
    }
    return 1;
}

static void
terminal_beep(int on) {
    static int beeping;
    if (on && !beeping) putchar('\a');
    beeping = on;
}

static void
terminal_draw(const chip8 *vm) {
    static const struct timespec frame = { 0, 1000000000L / 60 };
    const chip8regset *cpu = &vm->cpu;
    char buf[32];

    printf("\x1b[H");
    for (int y = 0; y < SCREEN_H; y += 2) {
        for (int x = 0; x < SCREEN_W; x++) {
            int top = vm->screen[y][x], bot = vm->screen[y + 1][x];
            fputs(top ? (bot ? "█" : "▀") : (bot ? "▄" : " "), stdout);
        }
        putchar('\n');
    }
    // Stato CPU
    chip8_disassemble(cpu->operand.WORD, buf, sizeof buf);
    printf("PC=%03X I=%03X SP=%03X DT=%02X ST=%02X  %04X %-16s\x1b[K\n",
           cpu->pc.WORD, cpu->i.WORD, cpu->sp.WORD, cpu->dt, cpu->st, cpu->operand.WORD, buf);
    for (int r = 0; r < 0x10; r++)
        printf("V%X=%02X%s", r, cpu->v[r], r == 7 ? "\x1b[K\n" : " ");
    printf("\x1b[K\n");
    fflush(stdout);

    nanosleep(&frame, NULL);
}

static int
terminal_close(const chip8 *vm) {
    (void) vm;
    term_restore();
    if (quit) {
        signal(quit, SIG_DFL);
        raise(quit);
    }
    return 1;
}

const frontend frontend_terminal = {
    "terminal", "display e stato della CPU nel terminale",
    terminal_init, terminal_poll, terminal_beep, terminal_draw, terminal_close
};
