#include <stdio.h>
#include "../chip8.h"

void
chip8_disassemble(unsigned short op, char *out, size_t len) {
    static const char *alu[0x10] = {
        "LD", "OR", "AND", "XOR", "ADD", "SUB", "SHR", "SUBN",
        NULL, NULL, NULL, NULL, NULL, NULL, "SHL", NULL
    };
    int x = OP_X(op), y = OP_Y(op), n = OP_N(op), kk = OP_KK(op), nnn = OP_NNN(op);

    snprintf(out, len, "DW   0x%04X", op);
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

void
chip8_disassemble_rom(const chip8 *vm, int size) {
    char buf[32];
    word op;
    for (int a = PROGRAM_START; a < PROGRAM_START + size; a += 2) {
        op.BYTE.high = vm->memory[a & 0xFFF];
        op.BYTE.low  = vm->memory[(a + 1) & 0xFFF];
        chip8_disassemble(op.WORD, buf, sizeof buf);
        printf("%03X: %04X  %s\n", a, op.WORD, buf);
    }
}
