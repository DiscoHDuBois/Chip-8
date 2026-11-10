// core emulatore
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chip8.h"

//memoria
static inline byte
memory_read(const chip8 *vm, register int address) {
    return vm->memory[address & 0xFFF];        // bus indirizzi a 12 bit
}
static inline void
memory_write(chip8 *vm, register word address, register byte value) {
    vm->memory[address.WORD & 0xFFF] = value;
}

// Sprite 4x5 delle cifre esadecimali, usati da LD F, Vx
static const byte fontset[80] = {
    0xF0,0x90,0x90,0x90,0xF0, 0x20,0x60,0x20,0x20,0x70, // 0 1
    0xF0,0x10,0xF0,0x80,0xF0, 0xF0,0x10,0xF0,0x10,0xF0, // 2 3
    0x90,0x90,0xF0,0x10,0x10, 0xF0,0x80,0xF0,0x10,0xF0, // 4 5
    0xF0,0x80,0xF0,0x90,0xF0, 0xF0,0x10,0x20,0x40,0x40, // 6 7
    0xF0,0x90,0xF0,0x90,0xF0, 0xF0,0x90,0xF0,0x10,0xF0, // 8 9
    0xF0,0x90,0xF0,0x90,0x90, 0xE0,0x90,0xE0,0x90,0xE0, // A B
    0xF0,0x80,0x80,0x80,0xF0, 0xE0,0x90,0x90,0x90,0xE0, // C D
    0xF0,0x80,0xF0,0x80,0xF0, 0xF0,0x80,0xF0,0x80,0x80  // E F
};

/*
Demo: conta da 0 a F mostrando la cifra al centro.
A sinistra il codice macchina, a destra assembly.
*/
static const byte demo_rom[] = {
    0x60,0x00, // 200: LD   V0, 0x00     ; contatore
    0x61,0x1C, // 202: LD   V1, 0x1C     ; x = 28
    0x62,0x0D, // 204: LD   V2, 0x0D     ; y = 13
    0x00,0xE0, // 206: CLS               ; loop:
    0xF0,0x29, // 208: LD   F, V0        ; I = sprite della cifra V0
    0xD1,0x25, // 20A: DRW  V1, V2, 5
    0x22,0x16, // 20C: CALL 0x216        ; attesa
    0x70,0x01, // 20E: ADD  V0, 0x01
    0x64,0x0F, // 210: LD   V4, 0x0F
    0x80,0x42, // 212: AND  V0, V4       ; V0 = V0 mod 16
    0x12,0x06, // 214: JP   0x206
    0x63,0x1E, // 216: LD   V3, 0x1E     ; attesa: 30 frame = 0.5 s
    0xF3,0x15, // 218: LD   DT, V3
    0xF3,0x07, // 21A: LD   V3, DT
    0x33,0x00, // 21C: SE   V3, 0x00     ; salta se il timer è a 0
    0x12,0x1A, // 21E: JP   0x21A
    0x00,0xEE  // 220: RET
};

//Stack: il PC viene salvato in memoria un byte alla volta

static int
stack_push(chip8 *vm, word value) {
    chip8regset *cpu = &vm->cpu;
    if (cpu->sp.WORD >= STACK_END) return 0;                 // overflow
    memory_write(vm, cpu->sp, value.BYTE.high); cpu->sp.WORD++;
    memory_write(vm, cpu->sp, value.BYTE.low);  cpu->sp.WORD++;
    return 1;
}
static int
stack_pop(chip8 *vm, word *value) {
    chip8regset *cpu = &vm->cpu;
    if (cpu->sp.WORD <= STACK_START) return 0;
    cpu->sp.WORD -= 2;
    value->BYTE.high = memory_read(vm, cpu->sp.WORD);
    value->BYTE.low  = memory_read(vm, cpu->sp.WORD + 1);
    return 1;
}

// Riporta la macchina allo stato di accensione: va chiamata prima di caricare una ROM
void
chip8_reset(chip8 *vm) {
    memset(vm, 0, sizeof *vm);
    vm->cpu.pc.WORD = PROGRAM_START;
    vm->cpu.sp.WORD = STACK_START;
    vm->key_event = -1;
    memcpy(vm->memory + FONT_START, fontset, sizeof fontset);
}

// Ferma la CPU lasciando il PC sull'istruzione che ha causato l'errore
static int
cpu_halt(chip8 *vm, const char *reason) {
    vm->error = reason;
    vm->cpu.pc.WORD -= 2;
    return 0;
}

//Ciclo Fetch - Decode - Execute di una singola istruzione (ritorna 0 se la CPU si ferma)
int
chip8_cycle(chip8 *vm) {
    chip8regset *cpu = &vm->cpu;

    // FETCH
    cpu->operand.BYTE.high = memory_read(vm, cpu->pc.WORD);
    cpu->operand.BYTE.low  = memory_read(vm, cpu->pc.WORD + 1);
    cpu->pc.WORD += 2;

    // DECODE
    unsigned short op = cpu->operand.WORD;
    byte x = OP_X(op), y = OP_Y(op), n = OP_N(op), kk = OP_KK(op);
    unsigned short nnn = OP_NNN(op);
    byte *v = cpu->v;
    byte flag;

    // EXECUTE
    switch (op >> 12) {
    case 0x0:
        if (op == 0x00E0) memset(vm->screen, 0, sizeof vm->screen); // CLS
        else if (op == 0x00EE && !stack_pop(vm, &cpu->pc))           // RET
            return cpu_halt(vm, "stack underflow (RET senza CALL)");
        break;                                                             // SYS ignorata
    case 0x1: cpu->pc.WORD = nnn; break;                                   // JP
    case 0x2:                                                              // CALL
        if (!stack_push(vm, cpu->pc)) return cpu_halt(vm, "stack overflow (troppe CALL annidate)");
        cpu->pc.WORD = nnn;
        break;
    case 0x3: if (v[x] == kk) cpu->pc.WORD += 2; break;           // SE  Vx, kk
    case 0x4: if (v[x] != kk) cpu->pc.WORD += 2; break;           // SNE Vx, kk
    case 0x5: if (v[x] == v[y]) cpu->pc.WORD += 2; break;         // SE  Vx, Vy
    case 0x6: v[x] = kk; break;                                   // LD  Vx, kk
    case 0x7: v[x] += kk; break;                                  // ADD Vx, kk (senza carry)
    case 0x8:
        // ALU: VF si scrive per ultimo, così vale il flag anche se x == F
        switch (n) {
        case 0x0: v[x] = v[y]; break;
        case 0x1: v[x] |= v[y]; break;
        case 0x2: v[x] &= v[y]; break;
        case 0x3: v[x] ^= v[y]; break;
        case 0x4: flag = (v[x] + v[y]) > 0xFF; v[x] += v[y];       v[0xF] = flag; break; // carry
        case 0x5: flag = v[x] >= v[y];         v[x] -= v[y];       v[0xF] = flag; break; // NOT borrow
        case 0x6: flag = v[x] & 0x01;          v[x] >>= 1;         v[0xF] = flag; break; // bit uscito
        case 0x7: flag = v[y] >= v[x];         v[x] = v[y] - v[x]; v[0xF] = flag; break;
        case 0xE: flag = v[x] >> 7;            v[x] <<= 1;         v[0xF] = flag; break;
        }
        break;
    case 0x9: if (v[x] != v[y]) cpu->pc.WORD += 2; break;         // SNE Vx, Vy
    case 0xA: cpu->i.WORD = nnn; break;                           // LD  I, nnn
    case 0xB: cpu->pc.WORD = nnn + v[0]; break;                   // JP  V0, nnn
    case 0xC: v[x] = (rand() & 0xFF) & kk; break;                 // RND Vx, kk
    case 0xD: {                                                   // DRW Vx, Vy, n
        // XOR PER SCRIVERE I PIXEL
        /*
        Ogni riga dello sprite è un byte (8 pixel). Il pixel viene invertito (XOR):
        se era acceso e si spegne c'è una collisione e VF = 1
        */
        int px = v[x] % SCREEN_W, py = v[y] % SCREEN_H;
        v[0xF] = 0;
        for (int row = 0; row < n && py + row < SCREEN_H; row++) {
            byte sprite = memory_read(vm, cpu->i.WORD + row);
            for (int col = 0; col < 8 && px + col < SCREEN_W; col++) {
                if (sprite & (0x80 >> col)) {
                    byte *pixel = &vm->screen[py + row][px + col];
                    if (*pixel) v[0xF] = 1;
                    *pixel ^= 1;
                }
            }
        }
        break;
    }
    case 0xE:
        if (kk == 0x9E && vm->keys[v[x] & 0xF])  cpu->pc.WORD += 2; // SKP  Vx
        if (kk == 0xA1 && !vm->keys[v[x] & 0xF]) cpu->pc.WORD += 2; // SKNP Vx
        break;
    case 0xF:
        switch (kk) {
        case 0x07: v[x] = cpu->dt; break;                         // LD Vx, DT
        case 0x0A:                                                // LD Vx, K (attesa nuova pressione)
            if (vm->key_event >= 0) {
                v[x] = vm->key_event;
                vm->key_event = -1;
            }
            else cpu->pc.WORD -= 2;                               // ripete l'istruzione
            break;

        case 0x15: cpu->dt = v[x]; break;                         // LD DT, Vx
        case 0x18: cpu->st = v[x]; break;                         // LD ST, Vx
        case 0x1E: cpu->i.WORD += v[x]; break;                    // ADD I, Vx
        case 0x29: cpu->i.WORD = FONT_START + (v[x] & 0xF) * 5; break; // LD F, Vx
        case 0x33: {                                              // LD B, Vx (BCD: centinaia, decine, unità)
            word a = cpu->i;
            memory_write(vm, a, v[x] / 100);      a.WORD++;
            memory_write(vm, a, v[x] / 10 % 10);  a.WORD++;
            memory_write(vm, a, v[x] % 10);
            break;
        }
        case 0x55: {                                              // LD [I], Vx (I invariato)
            word a = cpu->i;
            for (int r = 0; r <= x; r++, a.WORD++) memory_write(vm, a, v[r]);
            break;
        }
        case 0x65:                                                // LD Vx, [I] (I invariato)
            for (int r = 0; r <= x; r++) v[r] = memory_read(vm, cpu->i.WORD + r);
            break;
        }
        break;
    }
    return 1;
}

// I timer scendono a 60 Hz, indipendentemente dalla velocità della CPU
void
chip8_timers(chip8 *vm) {
    if (vm->cpu.dt) vm->cpu.dt--;
    if (vm->cpu.st) vm->cpu.st--;
}

int
chip8_load_rom(chip8 *vm, const char *path) {
    const size_t max = STACK_START - PROGRAM_START;   // ROM NON deve invadere lo stack
    FILE *f = fopen(path, "rb");
    if (f == NULL) { perror(path); return -1; }
    size_t size = fread(vm->memory + PROGRAM_START, 1, max, f);
    int extra = size == max ? fgetc(f) : EOF;        // anche un solo bit in più -> ROM troppo grande
    if (ferror(f)) { perror(path); fclose(f); return -1; }
    fclose(f);
    if (extra != EOF) { fprintf(stderr, "%s: ROM troppo grande (max %zu byte)\n", path, max); return -1; }
    if (size == 0)    { fprintf(stderr, "%s: ROM vuota\n", path); return -1; }
    return (int) size;
}

int
chip8_load_demo(chip8 *vm) {
    memcpy(vm->memory + PROGRAM_START, demo_rom, sizeof demo_rom);
    return sizeof demo_rom;
}
