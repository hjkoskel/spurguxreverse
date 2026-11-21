# SpurguX v1.17 -- Commodore 64 port
#
#   make          spurgu64.prg and spurgu64.d64 (cc65, python3, c1541)
#   make run      start the disk image in VICE (x64sc)
#   make size     segment sizes from the linker map
#   make sfxtest  sfxtest.prg: one key per sound effect (tools/mkfx.py)
CL65    ?= cl65
C1541   ?= c1541
X64     ?= x64sc
PYTHON  ?= python3
CFLAGS   = -t c64 -Os -Cl -g -W +unused-param -I build
LDFLAGS  = -t c64 -C spurgu64.cfg -m spurgu64.map -Ln spurgu64.lbl
SRCS     = c64.c data.c game.c main.c
DATA     = data
GEN      = build

all: spurgu64.d64

glyphs.h: tools/mkglyphs.py
	$(PYTHON) tools/mkglyphs.py $@

$(GEN)/sfx.inc $(GEN)/sfx.h: tools/mkfx.py
	@mkdir -p $(GEN)
	$(PYTHON) tools/mkfx.py $(GEN)/sfx.inc $(GEN)/sfx.h

$(GEN)/%.o: %.c spurgux.h glyphs.h $(GEN)/sfx.h
	@mkdir -p $(GEN)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(GEN)/%.o: %.s
	@mkdir -p $(GEN)
	$(CL65) -t c64 -c -o $@ $<

$(GEN)/sound.o: sound.s $(GEN)/sfx.inc
	$(CL65) -t c64 --asm-include-dir $(GEN) -c -o $@ $<

OBJS     = $(SRCS:%.c=$(GEN)/%.o) $(GEN)/drawrow.o $(GEN)/flood.o $(GEN)/sound.o

spurgu64.prg: $(OBJS) spurgu64.cfg
	$(CL65) $(LDFLAGS) -o $@ $(OBJS)

$(GEN)/spu.tip: $(DATA)/SPU.TIP tools/mkdata.py
	@mkdir -p $(GEN)
	$(PYTHON) tools/mkdata.py cp437 $< $@
$(GEN)/spu.spe: $(DATA)/SPU.SPE tools/mkdata.py
	@mkdir -p $(GEN)
	$(PYTHON) tools/mkdata.py cp437 $< $@
$(GEN)/spu.hlp: data/spu_hlp.txt tools/mkdata.py
	@mkdir -p $(GEN)
	$(PYTHON) tools/mkdata.py utf8 $< $@

spurgu64.d64: spurgu64.prg $(GEN)/spu.tip $(GEN)/spu.spe $(GEN)/spu.hlp
	rm -f $@
	$(C1541) -format "spurgux,64" d64 $@ \
	  -write spurgu64.prg spurgu64 \
	  -write $(GEN)/spu.hlp "spu.hlp,s" \
	  -write $(GEN)/spu.spe "spu.spe,s" \
	  -write $(GEN)/spu.tip "spu.tip,s" > /dev/null

run: spurgu64.d64
	$(X64) -autostart spurgu64.d64

size: spurgu64.prg
	@sed -n '/^Segment list/,/^$$/p' spurgu64.map
	@ls -l spurgu64.prg

sfxtest: sfxtest.prg
	$(X64) -autostart sfxtest.prg

sfxtest.prg: tools/sfxtest.c $(GEN)/sound.o $(GEN)/sfx.h
	$(CL65) $(CFLAGS) -c -o $(GEN)/sfxtest.o tools/sfxtest.c
	$(CL65) -t c64 -o $@ $(GEN)/sfxtest.o $(GEN)/sound.o

clean:
	rm -rf $(GEN) spurgu64.prg spurgu64.d64 spurgu64.map spurgu64.lbl sfxtest.prg

.PHONY: all run size sfxtest clean
