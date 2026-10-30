#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>

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
#define KEY_HOLD_FRAMES  10      // il terminale non segnala il rilascio: tasto "premuto" per N frame

//memoria
static byte *memory;

int
memory_init(int memorysize) {
    memory = (byte *) calloc(memorysize, sizeof(byte));
    return memory != NULL;
}
static inline byte
memory_read(register int address) {
    return memory[address & 0xFFF];        // bus indirizzi a 12 bit
}
static inline void
memory_write(register word address, register byte value) {
    memory[address.WORD & 0xFFF] = value;
}

//CPU Vera e propria Virtualizzata
/*
Una CPU CHIP-8 contiene 16 registri di uso generale da 8bit, rappresentabili con un array
*/
typedef struct {
    byte v[0x10]; // <- Registri V0..VF (VF = flag di carry/borrow/collisione)
    word i;       // <- Registro Indice I (puntatore in memoria)
    word sp;      // <- Stack Pointer
    word pc;      // <- Program Counter
    byte dt;      // <- Delay Timer
    byte st;      // <- Sound Timer
    word operand; // <- Operando Corrente (istruzione da 2 byte)
} chip8regset;

static chip8regset cpu;
static byte screen[SCREEN_H][SCREEN_W];  // 1 = pixel acceso
static byte keys[0x10];                  // >0 = tasto premuto
static int key_event = -1;
static const char *cpu_error;            // != NULL --> 'arresto della CPU

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
A sinistra il codice macchina, a destra l'assembly.
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

// Campi dell'istruzione: es. 0xDXYN, 0x6XKK, 0x1NNN
#define OP_X(op)   (((op) >> 8) & 0xF)
#define OP_Y(op)   (((op) >> 4) & 0xF)
#define OP_N(op)   ((op) & 0xF)
#define OP_KK(op)  ((op) & 0xFF)
#define OP_NNN(op) ((op) & 0xFFF)

//Disassemblatore: codice macchina -> mnemonico assembly
static void
disassemble(unsigned short op, char *out, size_t len) {
    static const char *alu[0x10] = {
        "LD", "OR", "AND", "XOR", "ADD", "SUB", "SHR", "SUBN",
        NULL, NULL, NULL, NULL, NULL, NULL, "SHL", NULL
    };
    int x = OP_X(op), y = OP_Y(op), n = OP_N(op), kk = OP_KK(op), nnn = OP_NNN(op);

    snprintf(out, len, "DW   0x%04X", op);   // default: dato, non istruzione
    switch (op >> 12) {
    case 0x0:
        if (op == 0x00E0)      snprintf(out, len, "CLS");
        else if (op == 0x00EE) snprintf(out, len, "RET");
        else                   snprintf(out, len, "SYS  0x%03X", nnn);
        break;
    case 0x1: snprintf(out, len, "JP   0x%03X", nnn); break;
    case 0x2: snprintf(out, len, "CALL 0x%03X", nnn); break;
    case 0x3: snprintf(out, len, "SE   V%X, 0x%02X", x, kk); break;
    case 0x4: snprintf(out, len, "SNE  V%X, 0x%02X", x, kk); break;
    case 0x5: if (n == 0) snprintf(out, len, "SE   V%X, V%X", x, y); break;
    case 0x6: snprintf(out, len, "LD   V%X, 0x%02X", x, kk); break;
    case 0x7: snprintf(out, len, "ADD  V%X, 0x%02X", x, kk); break;
    case 0x8: if (alu[n]) snprintf(out, len, "%-4s V%X, V%X", alu[n], x, y); break;
    case 0x9: if (n == 0) snprintf(out, len, "SNE  V%X, V%X", x, y); break;
    case 0xA: snprintf(out, len, "LD   I, 0x%03X", nnn); break;
    case 0xB: snprintf(out, len, "JP   V0, 0x%03X", nnn); break;
    case 0xC: snprintf(out, len, "RND  V%X, 0x%02X", x, kk); break;
    case 0xD: snprintf(out, len, "DRW  V%X, V%X, %d", x, y, n); break;
    case 0xE:
        if (kk == 0x9E)      snprintf(out, len, "SKP  V%X", x);
        else if (kk == 0xA1) snprintf(out, len, "SKNP V%X", x);
        break;
    case 0xF:
        switch (kk) {
        case 0x07: snprintf(out, len, "LD   V%X, DT", x); break;
        case 0x0A: snprintf(out, len, "LD   V%X, K", x); break;
        case 0x15: snprintf(out, len, "LD   DT, V%X", x); break;
        case 0x18: snprintf(out, len, "LD   ST, V%X", x); break;
        case 0x1E: snprintf(out, len, "ADD  I, V%X", x); break;
        case 0x29: snprintf(out, len, "LD   F, V%X", x); break;
        case 0x33: snprintf(out, len, "LD   B, V%X", x); break;
        case 0x55: snprintf(out, len, "LD   [I], V%X", x); break;
        case 0x65: snprintf(out, len, "LD   V%X, [I]", x); break;
        }
        break;
    }
}

//Stack: il PC viene salvato in memoria un byte alla volta

static int
stack_push(word value) {
    if (cpu.sp.WORD >= STACK_END) return 0;                  // overflow
    memory_write(cpu.sp, value.BYTE.high); cpu.sp.WORD++;
    memory_write(cpu.sp, value.BYTE.low);  cpu.sp.WORD++;
    return 1;
}
static int
stack_pop(word *value) {
    if (cpu.sp.WORD <= STACK_START) return 0;                // underflow: RET senza CALL
    cpu.sp.WORD -= 2;
    value->BYTE.high = memory_read(cpu.sp.WORD);
    value->BYTE.low  = memory_read(cpu.sp.WORD + 1);
    return 1;
}

static void
cpu_reset(void) {
    memset(&cpu, 0, sizeof cpu);
    memset(screen, 0, sizeof screen);
    cpu.pc.WORD = PROGRAM_START;
    cpu.sp.WORD = STACK_START;
    memcpy(memory + FONT_START, fontset, sizeof fontset);
}

// Ferma la CPU lasciando il PC sull'istruzione che ha causato l'errore
static int
cpu_halt(const char *reason) {
    cpu_error = reason;
    cpu.pc.WORD -= 2;
    return 0;
}

//Ciclo Fetch - Decode - Execute di una singola istruzione (ritorna 0 se la CPU si ferma)
static int
cpu_cycle(void) {
    // FETCH
    cpu.operand.BYTE.high = memory_read(cpu.pc.WORD);
    cpu.operand.BYTE.low  = memory_read(cpu.pc.WORD + 1);
    cpu.pc.WORD += 2;

    // DECODE
    unsigned short op = cpu.operand.WORD;
    byte x = OP_X(op), y = OP_Y(op), n = OP_N(op), kk = OP_KK(op);
    unsigned short nnn = OP_NNN(op);
    byte *v = cpu.v;
    byte flag;

    // EXECUTE
    switch (op >> 12) {
    case 0x0:
        if (op == 0x00E0) memset(screen, 0, sizeof screen);       // CLS
        else if (op == 0x00EE && !stack_pop(&cpu.pc))             // RET
            return cpu_halt("stack underflow (RET senza CALL)");
        break;                                                    // SYS ignorata (codice nativo 1802)
    case 0x1: cpu.pc.WORD = nnn; break;                           // JP
    case 0x2:                                                     // CALL
        if (!stack_push(cpu.pc)) return cpu_halt("stack overflow (troppe CALL annidate)");
        cpu.pc.WORD = nnn;
        break;
    case 0x3: if (v[x] == kk) cpu.pc.WORD += 2; break;            // SE  Vx, kk
    case 0x4: if (v[x] != kk) cpu.pc.WORD += 2; break;            // SNE Vx, kk
    case 0x5: if (v[x] == v[y]) cpu.pc.WORD += 2; break;          // SE  Vx, Vy
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
    case 0x9: if (v[x] != v[y]) cpu.pc.WORD += 2; break;          // SNE Vx, Vy
    case 0xA: cpu.i.WORD = nnn; break;                            // LD  I, nnn
    case 0xB: cpu.pc.WORD = nnn + v[0]; break;                    // JP  V0, nnn
    case 0xC: v[x] = (rand() & 0xFF) & kk; break;                 // RND Vx, kk
    case 0xD: {                                                   // DRW Vx, Vy, n
        //UTILIZZO DI XOR PER SCRIVERE I PIXEL
        /*
        Ogni riga dello sprite è un byte (8 pixel). Il pixel viene invertito (XOR):
        se era acceso e si spegne c'è una collisione e VF = 1
        */
        int px = v[x] % SCREEN_W, py = v[y] % SCREEN_H;
        v[0xF] = 0;
        for (int row = 0; row < n && py + row < SCREEN_H; row++) {
            byte sprite = memory_read(cpu.i.WORD + row);
            for (int col = 0; col < 8 && px + col < SCREEN_W; col++) {
                if (sprite & (0x80 >> col)) {
                    byte *pixel = &screen[py + row][px + col];
                    if (*pixel) v[0xF] = 1;
                    *pixel ^= 1;
                }
            }
        }
        break;
    }
    case 0xE:
        if (kk == 0x9E && keys[v[x] & 0xF])  cpu.pc.WORD += 2;    // SKP  Vx
        if (kk == 0xA1 && !keys[v[x] & 0xF]) cpu.pc.WORD += 2;    // SKNP Vx
        break;
    case 0xF:
        switch (kk) {
        case 0x07: v[x] = cpu.dt; break;                          // LD Vx, DT
        case 0x0A:                                                // LD Vx, K (attesa nuova pressione)
            if (key_event >= 0) {
                v[x] = key_event;
                key_event = -1;
            }
            else cpu.pc.WORD -= 2;                                // ripete l'istruzione
            break;
        
        case 0x15: cpu.dt = v[x]; break;                          // LD DT, Vx
        case 0x18: cpu.st = v[x]; break;                          // LD ST, Vx
        case 0x1E: cpu.i.WORD += v[x]; break;                     // ADD I, Vx
        case 0x29: cpu.i.WORD = FONT_START + (v[x] & 0xF) * 5; break; // LD F, Vx
        case 0x33: {                                              // LD B, Vx (BCD: centinaia, decine, unità)
            word a = cpu.i;
            memory_write(a, v[x] / 100);      a.WORD++;
            memory_write(a, v[x] / 10 % 10);  a.WORD++;
            memory_write(a, v[x] % 10);
            break;
        }
        case 0x55: {                                              // LD [I], Vx (I invariato)
            word a = cpu.i;
            for (int r = 0; r <= x; r++, a.WORD++) memory_write(a, v[r]);
            break;
        }
        case 0x65:                                                // LD Vx, [I] (I invariato)
            for (int r = 0; r <= x; r++) v[r] = memory_read(cpu.i.WORD + r);
            break;
        }
        break;
    }
    return 1;
}

//Caricamento programma
static int
rom_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) { perror(path); return -1; }
    size_t size = fread(memory + PROGRAM_START, 1, STACK_START - PROGRAM_START, f);
    fclose(f);
    return (int) size;
}

static int
demo_load(void) {
    memcpy(memory + PROGRAM_START, demo_rom, sizeof demo_rom);
    return sizeof demo_rom;
}

static void
disassemble_rom(int size) {
    char buf[32];
    word op;
    for (int a = PROGRAM_START; a < PROGRAM_START + size; a += 2) {
        op.BYTE.high = memory_read(a);
        op.BYTE.low  = memory_read(a + 1);
        disassemble(op.WORD, buf, sizeof buf);
        printf("%03X: %04X  %s\n", a, op.WORD, buf);
    }
}

//Terminale: input non bloccante e output con sequenze ANSI
static struct termios term_orig;
static int term_raw;

static void
term_restore(void) {
    if (term_raw) tcsetattr(STDIN_FILENO, TCSANOW, &term_orig);
    printf("\x1b[?25h\n");
    fflush(stdout);
}

static void
on_signal(int sig) {
    (void) sig;
    exit(0);
}

static void
term_setup(void) {
    atexit(term_restore);
    signal(SIGINT, on_signal);
    if (tcgetattr(STDIN_FILENO, &term_orig) == 0) {
        struct termios raw = term_orig;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        term_raw = tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0;
    }
    printf("\x1b[2J\x1b[?25l"); //per pulizia
}

//map
// Tastierino CHIP-8   ->  tastiera PC
//   1 2 3 C               1 2 3 4
//   4 5 6 D               q w e r
//   7 8 9 E               a s d f
//   A 0 B F               z x c v
static const char keymap[0x10] = {
    'x', '1', '2', '3', 'q', 'w', 'e', 'a',
    's', 'd', 'z', 'c', '4', 'r', 'f', 'v'
};

static int
input_poll(void) {                         // ESC -> ritorna 0
    char c;
    for (int k = 0; k < 0x10; k++) {
        if (keys[k]) keys[k]--;
    }
    key_event = -1;
    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == 27) return 0; // <- qui ESC
        for (int k = 0; k < 0x10; k++)
            if (keymap[k] == tolower((unsigned char) c)) {
                keys[k] = KEY_HOLD_FRAMES; 
                key_event = k;
            }
    }
    return 1;
}

static void
screen_render(void) {
    char buf[32];
    printf("\x1b[H");
    for (int y = 0; y < SCREEN_H; y += 2) { // 2 righe di pixel per riga di testo
        for (int x = 0; x < SCREEN_W; x++) {
            int top = screen[y][x], bot = screen[y + 1][x];
            fputs(top ? (bot ? "█" : "▀") : (bot ? "▄" : " "), stdout);
        }
        putchar('\n');
    }
    // Stato CPU
    disassemble(cpu.operand.WORD, buf, sizeof buf);
    printf("PC=%03X I=%03X SP=%03X DT=%02X ST=%02X  %04X %-16s\x1b[K\n",
           cpu.pc.WORD, cpu.i.WORD, cpu.sp.WORD, cpu.dt, cpu.st, cpu.operand.WORD, buf);
    for (int r = 0; r < 0x10; r++)
        printf("V%X=%02X%s", r, cpu.v[r], r == 7 ? "\x1b[K\n" : " ");
    printf("\x1b[K\n");
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    int disasm = 0, size, beeping = 0;
    const struct timespec frame = { 0, 1000000000L / 60 };

    if (argc > 1 && strcmp(argv[1], "-d") == 0) { disasm = 1; argc--; argv++; }

    if (!memory_init(MEMORY_SIZE)) { perror("memory_init"); return 1; }
    cpu_reset();
    size = argc > 1 ? rom_load(argv[1]) : demo_load();
    if (size < 0) return 1;

    if (disasm) {
        disassemble_rom(size);
        free(memory);
        return 0;
    }

    srand((unsigned) time(NULL));
    term_setup();

    int running = 1;
    while (running && input_poll()) {
        for (int c = 0; c < CYCLES_PER_FRAME && running; c++)
            running = cpu_cycle();
        if (cpu.dt) cpu.dt--;
        if (cpu.st) cpu.st--;
        if (cpu.st && !beeping) putchar('\a'); // beep
        beeping = cpu.st > 0;
        screen_render();                       // anche dopo un errore: mostra lo stato finale
        nanosleep(&frame, NULL);
    }

    if (cpu_error) {
        char buf[32];
        disassemble(cpu.operand.WORD, buf, sizeof buf);
        fprintf(stderr, "CPU fermata: %s a PC=%03X (%04X %s), SP=%03X\n",
                cpu_error, cpu.pc.WORD, cpu.operand.WORD, buf, cpu.sp.WORD);
    }
    free(memory);
    return cpu_error ? 1 : 0;
}
