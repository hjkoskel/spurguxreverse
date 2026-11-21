; sound.s -- SID sound effects, played from the KERNAL interrupt.
;
; The jiffy IRQ ($0314, 60 times a second) steps through an effect on SID
; voice 1, so the game never waits for a sound.  An effect is a list of
; 6-byte steps ended by a 0 byte:
;
;   frames, freq lo, freq hi, slide lo, slide hi, control register
;
; The frequency is set at the start of the step and the 16-bit slide is
; added to it every frame.  A control value with the gate bit clear starts
; the release (gaps between notes).  At the end of the effect the gate is
; cleared.
;
; SndPlay(id): an effect of higher priority than the one playing starts at
; once; otherwise it is played after it (one effect is kept waiting).
;
; The effects themselves are defined in tools/mkfx.py, which the Makefile
; runs to make build/sfx.inc (included below) and build/sfx.h (SND_* ids).

        .export _SndInit, _SndExit, _SndPlay

SID     = $D400
IRQVEC  = $0314

        .bss
oldirq: .res 2
active: .res 1          ; 1 while an effect is playing
curpri: .res 1          ; its priority
pending:.res 1          ; effect id + 1 waiting, 0 = none
timer:  .res 1          ; frames left in the current step
pos:    .res 1          ; offset of the next step
freq:   .res 2
slide:  .res 2
ctrl:   .res 1

        .code

; void SndInit(void)
_SndInit:
        lda     #0
        ldx     #$18
@clr:   sta     SID,x
        dex
        bpl     @clr
        sta     active
        sta     pending
        lda     #$08            ; 50 % pulse width
        sta     SID+3
        lda     #$0F            ; full volume
        sta     SID+$18
        sei
        lda     IRQVEC
        sta     oldirq
        lda     IRQVEC+1
        sta     oldirq+1
        lda     #<irq
        sta     IRQVEC
        lda     #>irq
        sta     IRQVEC+1
        cli
        rts

; void SndExit(void)
_SndExit:
        sei
        lda     oldirq
        sta     IRQVEC
        lda     oldirq+1
        sta     IRQVEC+1
        cli
        lda     #0
        sta     SID+4
        sta     SID+$18
        rts

; void __fastcall__ SndPlay(u8 id)
_SndPlay:
        tax
        sei
        lda     active
        beq     @now
        lda     prio,x
        cmp     curpri
        beq     @later
        bcs     @now
@later: inx
        stx     pending
        cli
        rts
@now:   jsr     start
        cli
        rts

; start effect X (interrupts off)
start:  lda     prio,x
        sta     curpri
        lda     #$08            ; gate off (the next gate on restarts ADSR)
        sta     SID+4           ; and TEST: resets the noise generator,
        lda     #0              ; which a waveform change can lock up
        sta     pos
        lda     ad,x
        sta     SID+5
        lda     sr,x
        sta     SID+6
        lda     lo,x
        sta     get+1
        lda     hi,x
        sta     get+2
        lda     #1
        sta     timer           ; first step on the next interrupt
        sta     active
        rts

; next byte of the effect: A = (effect),Y; Y++ (flags are Y's)
get:    lda     $FFFF,y
        iny
        rts

irq:    lda     active
        beq     @out
        clc
        lda     freq
        adc     slide
        sta     freq
        sta     SID
        lda     freq+1
        adc     slide+1
        sta     freq+1
        sta     SID+1
        dec     timer
        bne     @out
        ldy     pos
        jsr     get
        tax                     ; (INY in get changed the Z flag)
        beq     @end
        stx     timer
        jsr     get
        sta     freq
        sta     SID
        jsr     get
        sta     freq+1
        sta     SID+1
        jsr     get
        sta     slide
        jsr     get
        sta     slide+1
        jsr     get
        sta     ctrl
        sta     SID+4
        sty     pos
        jmp     (oldirq)
@end:   lda     ctrl
        and     #$FE
        sta     SID+4
        lda     #0
        sta     active
        ldx     pending
        beq     @out
        sta     pending
        dex
        jsr     start
@out:   jmp     (oldirq)

        .rodata
; prio, ad, sr, lo, hi (one byte per effect) and the step lists fx_*,
; generated from the effect definitions in tools/mkfx.py
        .include "sfx.inc"
