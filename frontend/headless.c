#include <stdio.h>
#include <stdlib.h>

#include "frontend.h"

static FILE *dump;

static int
headless_init(const frontend_config *cfg) {
    if (cfg->max_cycles <= 0) {          // senza limite una ROM che non termina girerebbe per sempre
        fprintf(stderr, "headless: serve -n N (numero di cicli da eseguire)\n");
        return 0;
    }
    // Il file si apre subito: un percorso sbagliato si scopre prima di eseguire la ROM
    dump = cfg->dump_path ? fopen(cfg->dump_path, "w") : stdout;
    if (dump == NULL) { perror(cfg->dump_path); return 0; }
    srand(1);                            // seme fisso: RND dà la stessa sequenza a ogni esecuzione
    return 1;
}

static int
headless_poll(chip8 *vm) {               // nessun tasto, nessuna richiesta di uscita
    (void) vm;
    return 1;
}

static void
headless_beep(int on) {
    (void) on;
}

static void
headless_draw(const chip8 *vm) {         // non disegna e non aspetta: il tempo è solo "emulato"
    (void) vm;
}

static int
headless_close(const chip8 *vm) {
    for (int y = 0; y < SCREEN_H; y++) {
        for (int x = 0; x < SCREEN_W; x++)
            fputc(vm->screen[y][x] ? '#' : '.', dump);
        fputc('\n', dump);
    }
    int ok = !ferror(dump);
    if (dump == stdout) ok = fflush(dump) == 0 && ok;
    else                ok = fclose(dump) == 0 && ok;
    if (!ok) fprintf(stderr, "headless: errore di scrittura dello schermo\n");
    return ok;
}

const frontend frontend_headless = {
    "headless", "nessuna finestra: esegue -n cicli e scrive lo schermo finale (-o)",
    headless_init, headless_poll, headless_beep, headless_draw, headless_close
};
