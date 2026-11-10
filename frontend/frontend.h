#ifndef FRONTEND_H
#define FRONTEND_H

#include "../chip8.h"


// Opzioni della riga di comando che interessano ai frontend
typedef struct {
    long        max_cycles;   // -n: la CPU si ferma dopo N istruzioni (0 = nessun limite)
    const char *dump_path;    // -o: file su cui headless scrive lo schermo finale (NULL = stdout)
} frontend_config;

typedef struct {
    const char *name;                           // nome per l'opzione -f
    const char *desc;                           // descrizione per l'aiuto
    int  (*init)(const frontend_config *cfg);   // 0 = errore (in quel caso close non viene chiamata)
    int  (*poll)(chip8 *vm);                    // aggiorna vm->keys e vm->key_event; 0 = l'utente vuole uscire
    void (*beep)(int on);                       // on != 0 finché il sound timer è diverso da zero
    void (*draw)(const chip8 *vm);              // mostra il frame e attende il prossimo (60 Hz)
    int  (*close)(const chip8 *vm);             // rilascia tutto; 0 = errore
} frontend;

extern const frontend frontend_terminal;
extern const frontend frontend_headless;
#ifdef HAVE_SDL2
extern const frontend frontend_sdl2;
#endif

#endif
