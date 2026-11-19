# SpurguX v1.17 -- C89 port
#
#   make            size-optimised build (gcc/clang on Linux)
#   make cp437      raw CP437 output (DOS / ANSI.SYS / retro terminals)
#   make nofloat    compile check with general-purpose registers only
#
# For another compiler override OPT and LDOPT, e.g.
#   make CC=tcc OPT=-O LDOPT=
CC      ?= cc
OPT     ?= -Os -ffunction-sections -fdata-sections \
           -fno-asynchronous-unwind-tables -fno-stack-protector
LDOPT   ?= -s -no-pie -Wl,--gc-sections -Wl,-z,noseparate-code \
           -Wl,--build-id=none -Wl,-z,norelro -Wl,--hash-style=gnu
WARN     = -std=c89 -pedantic -Wall -Wextra
SRCS     = data.c game.c main.c term.c

spurgux: $(SRCS) spurgux.h
	$(CC) $(WARN) $(OPT) $(CFLAGS) -o $@ $(SRCS) $(LDOPT) $(LDFLAGS)

cp437: $(SRCS) spurgux.h
	$(CC) $(WARN) $(OPT) $(CFLAGS) -DOUT_CP437 -o spurgux $(SRCS) $(LDOPT) $(LDFLAGS)

# fails if any floating point slipped into the game code
nofloat:
	for f in $(SRCS); do $(CC) $(WARN) -Os -mgeneral-regs-only -c $$f -o /dev/null || exit 1; done

clean:
	rm -f spurgux *.o

.PHONY: clean cp437 nofloat
