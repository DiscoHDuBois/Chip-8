#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "chip8.h"
#include "frontend/frontend.h"

// Frontend disponibili: il primo è quello usato se manca l'opzione -f
static const frontend *const frontends[] = {
    &frontend_terminal,
#ifdef HAVE_SDL2
    &frontend_sdl2,
#endif
    &frontend_headless
};
#define FRONTEND_COUNT (sizeof frontends / sizeof frontends[0])

static const frontend *
frontend_find(const char *name) {
    for (size_t f = 0; f < FRONTEND_COUNT; f++)
        if (strcmp(frontends[f]->name, name) == 0) return frontends[f];
    return NULL;
}

static void
usage(const char *prog) {
    fprintf(stderr,
            "uso: %s [-d] [-f frontend] [-n cicli] [-o file] [rom.ch8]\n"
            "  -d           disassembla la ROM invece di eseguirla\n"
            "  -f frontend  come mostrare la macchina (default: %s)\n"
            "  -n cicli     ferma la CPU dopo N istruzioni\n"
            "  -o file      headless: file su cui scrivere lo schermo finale (default: stdout)\n"
            "Senza ROM esegue la demo interna.\n"
            "Frontend:\n", prog, frontends[0]->name);
    for (size_t f = 0; f < FRONTEND_COUNT; f++)
        fprintf(stderr, "  %-10s %s\n", frontends[f]->name, frontends[f]->desc);
}

int main(int argc, char *argv[]) {
    const frontend *fe = frontends[0];
    frontend_config cfg = { 0, NULL };
    int disasm = 0, size, opt;
    char *end;
    chip8 vm;

    while ((opt = getopt(argc, argv, "df:n:o:h")) != -1) {
        switch (opt) {
        case 'd': disasm = 1; break;
        case 'f':
            fe = frontend_find(optarg);
            if (fe == NULL) {
                fprintf(stderr, "%s: frontend sconosciuto: %s\n", argv[0], optarg);
                usage(argv[0]);
                return 1;
            }
            break;
        case 'n':
            cfg.max_cycles = strtol(optarg, &end, 10);
            if (*optarg == '\0' || *end != '\0' || cfg.max_cycles <= 0) {
                fprintf(stderr, "%s: numero di cicli non valido: %s\n", argv[0], optarg);
                return 1;
            }
            break;
        case 'o': cfg.dump_path = optarg; break;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }
    if (argc - optind > 1) { usage(argv[0]); return 1; }

    chip8_reset(&vm);
    size = optind < argc ? chip8_load_rom(&vm, argv[optind]) : chip8_load_demo(&vm);
    if (size < 0) return 1;

    if (disasm) {
        chip8_disassemble_rom(&vm, size);
        return 0;
    }

    srand((unsigned) time(NULL));
    if (!fe->init(&cfg)) return 1;

    int running = 1;
    long cycles = 0;
    while (running) {
        vm.key_event = -1;                   // una pressione vale per un solo frame
        if (!fe->poll(&vm)) break;
        for (int c = 0; c < CYCLES_PER_FRAME && running; c++) {
            running = chip8_cycle(&vm);
            if (++cycles == cfg.max_cycles) running = 0;      // limite chiesto con -n
        }
        chip8_timers(&vm);
        fe->beep(vm.cpu.st > 0);
        fe->draw(&vm);
    }

    if (vm.error) {
        char buf[32];
        chip8_disassemble(vm.cpu.operand.WORD, buf, sizeof buf);
        fprintf(stderr, "CPU fermata: %s a PC=%03X (%04X %s), SP=%03X\n",
                vm.error, vm.cpu.pc.WORD, vm.cpu.operand.WORD, buf, vm.cpu.sp.WORD);
    }
    // Dopo un segnale il frontend terminale termina il processo dentro close
    int closed = fe->close(&vm);
    return vm.error || !closed ? 1 : 0;
}
