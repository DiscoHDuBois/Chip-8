#ifndef CHIP8_H
#define CHIP8_H

#include <stddef.h>

/*
Inizio col definire byte e words
*/
typedef unsigned char byte;

/*
le #ifdef condition servono perchè la macchina che esegue l'emulatore (non il CHIP-8)
può salvare il byte più significativo all'indirizzo più basso (big endian) o più alto (little endian, x86/ARM)
*/
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    #define WORDS_BIGENDIAN
#endif

typedef union { /*
                La Union serve per vedere gli stessi 16 bit come un'unica WORD o come 2 BYTE (high/low)
                */
    unsigned short int WORD;
    struct {
        #ifdef WORDS_BIGENDIAN
            byte high, low;
        #else
            byte low, high;
        #endif
    } BYTE;
} word;

#define MEMORY_SIZE      0x1000  // 4 KB
#define FONT_START       0x050   // sprite delle cifre 0-F
#define PROGRAM_START    0x200   // 0x000-0x1FF era riservato all'interprete
#define STACK_START      0xEA0   // stack in memoria, come sul COSMAC VIP
#define STACK_LEVELS     16      // CALL annidate massime (il VIP ne aveva 12)
#define STACK_END        (STACK_START + 2 * STACK_LEVELS)
#define SCREEN_W         64
#define SCREEN_H         32
#define CYCLES_PER_FRAME 10      // ~600 istruzioni/s a 60 Hz

// Campi dell'istruzione: es. 0xDXYN, 0x6XKK, 0x1NNN
#define OP_X(op)   (((op) >> 8) & 0xF)
#define OP_Y(op)   (((op) >> 4) & 0xF)
#define OP_N(op)   ((op) & 0xF)
#define OP_KK(op)  ((op) & 0xFF)
#define OP_NNN(op) ((op) & 0xFFF)

//Una CPU CHIP-8 contiene 16 registri di uso generale da 8bit

typedef struct {
    byte v[0x10]; // <- Registri V0..VF (VF = flag di carry/borrow/collisione)
    word i;       // <- Registro Indice I (poiner in memoria)
    word sp;      // <- Stack Pointer
    word pc;      // <- Program Counter
    byte dt;      // <- Delay Timer
    byte st;      // <- Sound Timer
    word operand; // <- Operando Corrente (istruzione da 2 byte)
} chip8regset;

/*
Macchina completa: CPU + memoria + periferiche (schermo e tastierino).
Niente variabili globali, core indipendente
*/

typedef struct {
    chip8regset cpu;
    byte memory[MEMORY_SIZE];
    byte screen[SCREEN_H][SCREEN_W];  // 1 = pixel acceso
    byte keys[0x10];                  // >0 = tasto premuto (lo scrive il frontend)
    int  key_event;                   // tasto premuto in questo frame, -1 = nessuno (serve a LD Vx, K)
    const char *error;                // != NULL --> arresto della CPU
} chip8;

// chip8.c
void chip8_reset(chip8 *vm);                        // azzera tutto (memoria compresa) e carica il font
int  chip8_load_rom(chip8 *vm, const char *path);   // carica un file a 0x200: ritorna la dimensione, -1 se errore
int  chip8_load_demo(chip8 *vm);                    // carica la demo interna: ritorna la dimensione
int  chip8_cycle(chip8 *vm);                        // esegue un'istruzione: ritorna 0 se la CPU si ferma (vm->error)
void chip8_timers(chip8 *vm);                       // decrementa DT e ST: va chiamata a 60 Hz

// tools/disasm.c
void chip8_disassemble(unsigned short op, char *out, size_t len);  // codice macchina
void chip8_disassemble_rom(const chip8 *vm, int size);             // stampa il listato da 0x200

#endif
