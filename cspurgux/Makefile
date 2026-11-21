# SpurguX v1.17 -- C89 port
#
#   make            size-optimised build (gcc/clang on Linux)
#   make cp437      raw CP437 output (DOS / ANSI.SYS / retro terminals)
#   make nofloat    compile check with general-purpose registers only
#   make texts      regenerate spudata.h from the original files in TEXTS
#
# For another compiler override OPT and LDOPT, e.g.
#   make CC=tcc OPT=-O LDOPT=
CC      ?= cc
OPT     ?= -Os -ffunction-sections -fdata-sections \
           -fno-asynchronous-unwind-tables -fno-stack-protector -fno-pie
LDOPT   ?= -s -no-pie -Wl,--gc-sections -Wl,-z,noseparate-code \
           -Wl,--build-id=none -Wl,-z,norelro -Wl,--hash-style=gnu
WARN     = -std=c89 -pedantic -Wall -Wextra
SRCS     = data.c game.c main.c term.c
HDRS     = spurgux.h spudata.h
TEXTS   ?= ../spurg117
PYTHON  ?= python3

spurgux: $(SRCS) $(HDRS)
	$(CC) $(WARN) $(OPT) $(CFLAGS) -o $@ $(SRCS) $(LDOPT) $(LDFLAGS)

cp437: $(SRCS) $(HDRS)
	$(CC) $(WARN) $(OPT) $(CFLAGS) -DOUT_CP437 -o spurgux $(SRCS) $(LDOPT) $(LDFLAGS)

# fails if any floating point slipped into the game code
nofloat:
	for f in $(SRCS); do $(CC) $(WARN) -Os -mgeneral-regs-only -c $$f -o /dev/null || exit 1; done

# spudata.h is in the repository; this needs SPU.HLP, SPU.SPE and SPU.TIP
texts:
	$(PYTHON) tools/mktexts.py $(TEXTS) spudata.h

clean:
	rm -f spurgux *.o

.PHONY: clean cp437 nofloat texts
