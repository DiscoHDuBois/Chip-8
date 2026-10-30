# CHIP-8 — Calcolatori Elettronici-Assembly

Emulatore CHIP-8 scritto in C99, in un unico file (`chip8.c`), senza librerie esterne.
Il display viene disegnato nel terminale.

Il programma nasce come esercizio dopo aver sostenuto l'esame di **Calcolatori Elettronici**.
Costruire via software una CPU: registri, memoria, stack,
ciclo fetch-decode-execute. Per vedere che succede sotto Assembly.

---

## Breve intro su CHIP-8

Il CHIP-8 è un linguaggio interpretato creato nel 1977 da Joseph Weisbecker per il
microcomputer **COSMAC VIP**, basato sulla CPU RCA 1802. In pratica è una
**macchina virtuale**: ha un suo set di istruzioni, i suoi registri e la sua memoria.
È abbastanza semplice da essere implementato per intero in poche centinaia di righe,
ma contiene tutti i concetti di una CPU reale.

| Caratteristica        | Valore                                   |
|-----------------------|------------------------------------------|
| Memoria               | 4 KB (indirizzi a 12 bit: `0x000–0xFFF`) |
| Registri generali     | 16 da 8 bit (`V0`–`VF`)                  |
| Istruzioni            | 35, tutte lunghe 2 byte, big endian      |
| Display               | 64×32 pixel monocromatici                |
| Input                 | Tastierino esadecimale a 16 tasti        |
| Timer                 | 2 da 8 bit, decrementati a 60 Hz         |

---

## Compilazione e uso

### Compilazione

```sh
cc -std=c99 -O2 -Wall -o chip8 chip8.c
```

| Opzione      | Significato                                         |
|--------------|-----------------------------------------------------|
| `-std=c99`   | usa lo standard C99                                 |
| `-O2`        | attiva le ottimizzazioni                            |
| `-Wall`      | mostra tutti i warning principali                   |
| `-o chip8`   | nome dell'eseguibile prodotto                       |

Per usare un debugger (`lldb`/`gdb`) compila con `-g -O0` al posto di `-O2`.

### Esecuzione

```sh
./chip8               # esegue la demo interna (conta da 0 a F)
./chip8 gioco.ch8     # esegue una ROM
./chip8 -d gioco.ch8  # disassembla la ROM in mnemonici assembly
./chip8 -d            # disassembla la demo interna
```

Serve un terminale di almeno 64 colonne con supporto UTF-8 (va bene quello UNIX).

**Tastiera**

```
Tastierino CHIP-8      Tastiera PC
  1 2 3 C               1 2 3 4
  4 5 6 D               q w e r
  7 8 9 E               a s d f
  A 0 B F               z x c v
```

`ESC` (oppure `Ctrl+C`) per uscire.

Sotto lo schermo l'emulatore mostra in tempo reale lo stato della CPU:
l'ultima istruzione eseguita (in esadecimale e in assembly) e tutti i registri.

```
PC=20C I=05A SP=EA0 DT=00 ST=00  D125 DRW  V1, V2, 5
V0=02 V1=1C V2=0D V3=00 V4=0F V5=00 V6=00 V7=00
V8=00 V9=00 VA=00 VB=00 VC=00 VD=00 VE=00 VF=00
```

---

## I registri

Nel codice tutti i registri stanno nella struttura `chip8regset`.

```c
typedef struct {
    byte v[0x10]; // V0..VF
    word i;       // registro indice
    word sp;      // stack pointer
    word pc;      // program counter
    byte dt;      // delay timer
    byte st;      // sound timer
    word operand; // istruzione corrente
} chip8regset;
```

### Registri di uso generale — `V0`…`VF`

Sono 16 registri da 8 bit. Contengono i dati su cui lavorano le istruzioni:
contatori, coordinate, risultati di calcoli.
Corrispondono a `AL`, `BL`, `CL`… dell'x86 o a `R0`…`R15` di ARM.

**`VF` è speciale**: le istruzioni aritmetiche e di disegno lo usano come registro dei flag.

| Istruzione       | Significato di `VF` dopo l'esecuzione        | Flag equivalente x86 |
|------------------|-----------------------------------------------|----------------------|
| `ADD Vx, Vy`     | 1 se c'è stato riporto (risultato > 255)      | CF (carry)           |
| `SUB Vx, Vy`     | 1 se **non** c'è stato prestito (`Vx >= Vy`)  | CF invertito         |
| `SHR` / `SHL`    | il bit uscito dallo shift                     | CF                   |
| `DRW`            | 1 se un pixel acceso è stato spento (collisione) | —                 |

Per questo un programma non dovrebbe usare `VF` per i propri dati.

### Registro Accumulatore

Molte CPU classiche hanno un **accumulatore**: un registro privilegiato che è sempre
operando e destinazione delle operazioni aritmetiche.
Esempi sono il registro `A` dell'8080/Z80, `AX` nelle istruzioni `MUL`/`DIV` dell'x86
e il registro `D` della RCA 1802 su cui girava il CHIP-8 originale.

```asm
; 8080: l'accumulatore è implicito
MOV A, B
ADD C        ; A = A + C
```

Il CHIP-8 **non ha un accumulatore dedicato**: ogni `Vx` può fare da accumulatore.
Le istruzioni sono a due operandi, e il primo è sia sorgente sia destinazione:

```asm
; CHIP-8: qualsiasi Vx accumula il risultato
ADD V3, V5   ; V3 = V3 + V5
```

È la stessa scelta delle architetture a registri generali (x86 a 32/64 bit, ARM, RISC-V).

### Program Counter — Il `PC`

Registro a 16 bit (ne servono 12) che contiene l'indirizzo della prossima istruzione.
Parte da `0x200`: lo spazio `0x000–0x1FF` era occupato dall'interprete sul COSMAC VIP.

- Dopo ogni fetch: `PC += 2` (ogni istruzione è lunga 2 byte).
- Le istruzioni di salto (`JP`, `CALL`, `RET`) scrivono direttamente nel `PC`.
- Le istruzioni di skip (`SE`, `SNE`, `SKP`, `SKNP`) fanno `PC += 2` in più,
  così saltano l'istruzione successiva.

Corrisponde a `IP`/`EIP`/`RIP` dell'x86 e a `PC` (`R15`) di ARM.

### Stack Pointer — `SP` e lo stack

Lo stack serve solo a `CALL` e `RET` per salvare e recuperare l'indirizzo di ritorno.
In questo emulatore lo stack sta **in memoria** a partire da `0xEA0`, come sul COSMAC VIP,
e cresce verso indirizzi più alti.

```
CALL 0x216   →   mem[SP] = PC.high ; SP++
                 mem[SP] = PC.low  ; SP++
                 PC = 0x216

RET          →   SP -= 2
                 PC.high = mem[SP] ; PC.low = mem[SP+1]
```

Il CHIP-8 non ha istruzioni `PUSH`/`POP` per i dati, a differenza dell'x86.
Inoltre lo stack dell'x86 cresce verso il basso.

Lo stack ha spazio per 16 livelli (`0xEA0–0xEBF`, il COSMAC VIP ne aveva 12).
Senza controlli, superato `0xFFF` il mascheramento a 12 bit farebbe ripartire lo stack
da `0x000`: dopo 216 livelli verrebbe sovrascritto il font e dopo 432 il programma.
Una `CALL` di troppo (overflow) o un `RET` senza `CALL` (underflow)
fermano l'emulatore con un messaggio che riporta `PC` e istruzione:

```
CPU fermata: stack overflow (troppe CALL annidate) a PC=200 (2200 CALL 0x200), SP=EC0
```

Nel codice: `stack_push()` e `stack_pop()`.

### Registro indice — `I`

Registro a 16 bit che contiene un indirizzo di memoria. È l'unico modo per leggere
e scrivere la RAM: le istruzioni non accettano un indirizzo arbitrario per i dati,
ma usano sempre quello contenuto in `I`.
È l'analogo dei registri `SI`/`DI`/`BX` dell'x86 usati per l'indirizzamento indiretto,
e ricorda il **MAR** (Memory Address Register) dei modelli di CPU studiati.

```asm
LD   I, 0x300   ; I punta a 0x300
LD   [I], V2    ; scrive V0, V1, V2 in 0x300, 0x301, 0x302
```

### Registro istruzione — `operand`

Contiene l'istruzione appena letta dalla memoria. È l'equivalente dell'IR
(Instruction Register) che i decoder usano per decidere cosa fare.

### Timer — `DT` e `ST`

Due registri da 8 bit che, se diversi da zero, vengono decrementati 60 volte al secondo,
indipendentemente dalla CPU.
- `DT` (delay timer) serve per attese e temporizzazioni.
- `ST` (sound timer) fa suonare un beep finché è diverso da zero.

Sono una versione semplificata dei timer hardware e delle periferiche di un calcolatore reale.

---

## Mappa della memoria

```
0x000 ┌──────────────────────────┐
      │ (riservato all'interprete)│
0x050 │ font esadecimale 0–F     │  80 byte, 5 per cifra
0x0A0 │                          │
0x200 ├──────────────────────────┤  ← PC iniziale
      │ programma (ROM)          │
      │                          │
0xEA0 ├──────────────────────────┤  ← SP iniziale
      │ stack (cresce ↓ in figura,│
      │ verso indirizzi più alti) │
0xFFF └──────────────────────────┘
```

Il display è tenuto in un array separato (`screen[32][64]`) e non in memoria.

---

## Il ciclo Fetch – Decode – Execute

Parte importante dell'emulatore è la funzione `cpu_cycle()`, che esegue un'istruzione:

1. **Fetch** — legge 2 byte all'indirizzo `PC` e incrementa il `PC`.
   ```c
   cpu.operand.BYTE.high = memory_read(cpu.pc.WORD);
   cpu.operand.BYTE.low  = memory_read(cpu.pc.WORD + 1);
   cpu.pc.WORD += 2;
   ```
2. **Decode** — estrae i campi dell'istruzione con maschere e shift.
   ```
   0x D 1 2 5
      │ │ │ └── N   = 5     (4 bit)
      │ │ └──── Y   = 2     (4 bit)
      │ └────── X   = 1     (4 bit)
      └──────── opcode = D  (4 bit)

   KK  = byte basso (8 bit)      es. 6XKK
   NNN = 12 bit bassi (indirizzo) es. 1NNN
   ```
3. **Execute** — uno `switch` sull'opcode modifica registri, memoria o schermo.

Il `main` ripete il ciclo 10 volte per frame, a 60 frame al secondo.
Così la CPU esegue circa 600 istruzioni al secondo, mentre i timer scendono a 60 Hz.

---

## Endianness e la `union word`

Le istruzioni CHIP-8 sono memorizzate in big endian: prima il byte più significativo.
(I calcolatori moderni (x86, ARM) sono invece little endian)

La `union` permette di vedere gli stessi 16 bit come un'unica `WORD` oppure come due `BYTE`:

```c
typedef union {
    unsigned short int WORD;
    struct {
    #ifdef WORDS_BIGENDIAN
        byte high, low;
    #else
        byte low, high;
    #endif
    } BYTE;
} word;
```

L'`#ifdef` sceglie l'ordine dei campi in base alla macchina che esegue l'emulatore.
Così `BYTE.high` è sempre il byte più significativo di `WORD`, su qualsiasi computer.
Quando l'emulatore scrive `operand.BYTE.high` e `operand.BYTE.low`,
`operand.WORD` contiene già l'istruzione nel valore corretto.

---

## Modi di indirizzamento

Il CHIP-8 ha pochi modi di indirizzamento, ma sono quelli classici:

| Modo           | Esempio CHIP-8     | Significato                  | Esempio x86        |
|----------------|--------------------|------------------------------|--------------------|
| Immediato      | `LD V1, 0x1C`      | costante nell'istruzione     | `mov al, 0x1C`     |
| A registro     | `LD V1, V2`        | operando in un registro      | `mov al, bl`       |
| Diretto        | `JP 0x206`         | indirizzo nell'istruzione    | `jmp 0x206`        |
| Indiretto      | `LD [I], V2`       | indirizzo in un registro     | `mov [si], al`     |
| Indicizzato    | `JP V0, 0x300`     | base + registro              | `mov al, [bx+0x300]` |
| Implicito      | `CLS`, `RET`       | nessun operando esplicito    | `ret`              |

---

## Set di istruzioni

Notazione: `X`, `Y` = numero di registro; `KK` = byte; `NNN` = indirizzo; `N` = 4 bit.

| Opcode | Assembly          | Effetto                                         |
|--------|-------------------|-------------------------------------------------|
| `00E0` | `CLS`             | Pulisce lo schermo                              |
| `00EE` | `RET`             | Ritorno da subroutine (`PC` = pop)              |
| `1NNN` | `JP NNN`          | `PC = NNN`                                      |
| `2NNN` | `CALL NNN`        | push `PC`, `PC = NNN`                           |
| `3XKK` | `SE Vx, KK`       | salta la prossima se `Vx == KK`                 |
| `4XKK` | `SNE Vx, KK`      | salta la prossima se `Vx != KK`                 |
| `5XY0` | `SE Vx, Vy`       | salta la prossima se `Vx == Vy`                 |
| `6XKK` | `LD Vx, KK`       | `Vx = KK`                                       |
| `7XKK` | `ADD Vx, KK`      | `Vx += KK` (senza flag)                         |
| `8XY0` | `LD Vx, Vy`       | `Vx = Vy`                                       |
| `8XY1` | `OR Vx, Vy`       | `Vx \|= Vy`                                     |
| `8XY2` | `AND Vx, Vy`      | `Vx &= Vy`                                      |
| `8XY3` | `XOR Vx, Vy`      | `Vx ^= Vy`                                      |
| `8XY4` | `ADD Vx, Vy`      | `Vx += Vy`, `VF` = carry                        |
| `8XY5` | `SUB Vx, Vy`      | `Vx -= Vy`, `VF` = not borrow                   |
| `8XY6` | `SHR Vx`          | `Vx >>= 1`, `VF` = bit uscito                   |
| `8XY7` | `SUBN Vx, Vy`     | `Vx = Vy - Vx`, `VF` = not borrow               |
| `8XYE` | `SHL Vx`          | `Vx <<= 1`, `VF` = bit uscito                   |
| `9XY0` | `SNE Vx, Vy`      | salta la prossima se `Vx != Vy`                 |
| `ANNN` | `LD I, NNN`       | `I = NNN`                                       |
| `BNNN` | `JP V0, NNN`      | `PC = NNN + V0`                                 |
| `CXKK` | `RND Vx, KK`      | `Vx = random & KK`                              |
| `DXYN` | `DRW Vx, Vy, N`   | disegna sprite di N righe da `[I]` in (Vx, Vy)  |
| `EX9E` | `SKP Vx`          | salta se il tasto `Vx` è premuto                |
| `EXA1` | `SKNP Vx`         | salta se il tasto `Vx` non è premuto            |
| `FX07` | `LD Vx, DT`       | `Vx = DT`                                       |
| `FX0A` | `LD Vx, K`        | attende un tasto e lo salva in `Vx`             |
| `FX15` | `LD DT, Vx`       | `DT = Vx`                                       |
| `FX18` | `LD ST, Vx`       | `ST = Vx`                                       |
| `FX1E` | `ADD I, Vx`       | `I += Vx`                                       |
| `FX29` | `LD F, Vx`        | `I` = indirizzo dello sprite della cifra `Vx`   |
| `FX33` | `LD B, Vx`        | scrive in `[I]` centinaia, decine, unità di `Vx` (BCD) |
| `FX55` | `LD [I], Vx`      | salva `V0..Vx` in memoria da `I`                |
| `FX65` | `LD Vx, [I]`      | carica `V0..Vx` dalla memoria da `I`            |

`0NNN` (`SYS`) chiamava codice nativo della 1802 ed è ignorata.

### Salti condizionati: skip + jump

Il CHIP-8 non ha un registro di stato con istruzioni tipo `JE`/`JNE`.
Il salto condizionato si costruisce con uno skip e un jump:

```asm
; CHIP-8                     ; x86 equivalente
SE   V3, 0x00                ; cmp  bl, 0
JP   0x21A                   ; jne  0x21A
```

### Disegno con XOR

`DRW` non "scrive" i pixel: li inverte (XOR). Disegnare due volte lo stesso sprite
nello stesso punto lo cancella. Se un pixel acceso si spegne, `VF = 1`:
è così che i giochi rilevano le collisioni.

---

## Il programma demo, riga per riga

La demo è nel vettore `demo_rom[]` di `chip8.c`. Conta da 0 a F mostrando la cifra a schermo:

```asm
200: 6000  LD   V0, 0x00     ; V0 = contatore
202: 611C  LD   V1, 0x1C     ; V1 = x (28)
204: 620D  LD   V2, 0x0D     ; V2 = y (13)
206: 00E0  CLS               ; loop:
208: F029  LD   F, V0        ; I = sprite della cifra in V0
20A: D125  DRW  V1, V2, 5    ; disegna 5 righe
20C: 2216  CALL 0x216        ; chiama la subroutine di attesa
20E: 7001  ADD  V0, 0x01     ; contatore++
210: 640F  LD   V4, 0x0F
212: 8042  AND  V0, V4       ; V0 = V0 mod 16 (maschera di bit)
214: 1206  JP   0x206        ; torna a loop
216: 631E  LD   V3, 0x1E     ; attesa: 30 frame = 0.5 s
218: F315  LD   DT, V3
21A: F307  LD   V3, DT       ; legge il timer
21C: 3300  SE   V3, 0x00     ; se è 0 salta il JP...
21E: 121A  JP   0x21A        ; ...altrimenti continua ad aspettare
220: 00EE  RET
```

Come esercizio di assemblaggio a mano, si può provare a ricavare ogni opcode dalla tabella.
Per esempio `ADD V0, 0x01` → formato `7XKK` → `X=0`, `KK=01` → `7001`.

---

## Mappa del codice

| Funzione            | Ruolo                                                     |
|---------------------|-----------------------------------------------------------|
| `memory_init/read/write` | RAM da 4 KB, indirizzi mascherati a 12 bit           |
| `cpu_reset`         | Azzera i registri, `PC = 0x200`, `SP = 0xEA0`, carica il font |
| `cpu_cycle`         | Fetch – decode – execute di una istruzione                |
| `stack_push/pop`    | Salvataggio e ripristino del `PC` per `CALL`/`RET`, con controllo dei limiti |
| `cpu_halt`          | Ferma la CPU in caso di errore (es. stack overflow)       |
| `disassemble`       | Codice macchina → mnemonico assembly                      |
| `disassemble_rom`   | Disassembla un'intera ROM (opzione `-d`)                  |
| `rom_load/demo_load`| Carica il programma a `0x200`                             |
| `input_poll`        | Legge la tastiera senza bloccare                          |
| `screen_render`     | Disegna lo schermo e lo stato della CPU nel terminale     |
| `main`              | Ciclo principale a 60 Hz: input, CPU, timer, video        |

---

## Limiti noti per ora

- **Varianti delle istruzioni ("quirks")**: alcune istruzioni si comportano in modo diverso
  tra il CHIP-8 originale e le versioni successive. Qui gli shift agiscono direttamente su
  `Vx` e `FX55`/`FX65` lasciano invariato `I`, come nella maggior parte dei giochi.
  Alcune ROM vecchie potrebbero aspettarsi l'altro comportamento.
- **Tastiera**: il terminale non segnala il rilascio dei tasti, quindi ogni tasto resta
  "premuto" per 10 frame.
- **ESC**: anche le frecce inviano il carattere ESC, quindi chiudono l'emulatore.
- **Disassemblatore**: gli sprite memorizzati in mezzo al codice vengono letti come istruzioni
  (compaiono come `DW` o come istruzioni senza senso).
