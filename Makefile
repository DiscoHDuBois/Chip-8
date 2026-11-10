#VEDI README PER INFO

CC     ?= cc
CFLAGS ?= -O2
# Le opzioni obbligatorie stanno a parte, così "make CFLAGS=..." cambia solo l'ottimizzazione
FLAGS  := -std=c99 -Wall -Wextra

SRC := main.c chip8.c tools/disasm.c frontend/terminal.c frontend/headless.c

SDL ?= $(shell pkg-config --exists sdl2 2>/dev/null && echo 1 || echo 0)
ifeq ($(SDL),1)
SRC    += frontend/sdl2.c
FLAGS  += -DHAVE_SDL2
LDLIBS += $(shell pkg-config --libs sdl2)
build/frontend/sdl2.o: FLAGS += $(shell pkg-config --cflags sdl2)
endif

OBJ := $(SRC:%.c=build/%.o)

chip8: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

build/%.o: %.c chip8.h frontend/frontend.h
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) $(CFLAGS) -c -o $@ $<

test: chip8
	./chip8 -f headless -n 2000 -o build/demo.txt
	diff tests/demo.txt build/demo.txt
	@echo "test: OK"

clean:
	rm -rf build chip8

.PHONY: test clean
