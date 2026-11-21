; flood.s -- row routines of the reachability flood fill (game.c
; FixReachability).  A cell is "open" when it is not reached yet (bit 7
; clear) and is not a wall ($63), grave ($5B) or fence ($5C).
;
;   AlongRow: spreads reached cells along row FrCur, left to right and
;             right to left
;   FromRow:  marks open cells of row FrCur next to (also diagonally) a
;             reached cell of row FrNb
; Both return non-zero when a cell was marked.

        .export _AlongRow, _FromRow
        .import _FrCur, _FrNb
        .importzp ptr1, ptr2, ptr3, ptr4, tmp1, tmp2

MAPW    = 66
SEEN    = $80

; carry set when A is an open cell (A is kept)
isopen: cmp #SEEN
        bcs no
        cmp #$63
        beq no
        cmp #$5B
        beq no
        cmp #$5C
        beq no
        sec
        rts
no:     clc
        rts

_AlongRow:
        lda _FrCur
        sta ptr1
        lda _FrCur+1
        sta ptr1+1
        lda #0
        sta tmp1
        ldy #0                  ; left to right
        lda (ptr1),y
        sta tmp2                ; the previous cell
        iny
fwd:    lda (ptr1),y
        ldx tmp2
        bpl fstore
        jsr isopen
        bcc fstore
        ora #SEEN
        sta (ptr1),y
        inc tmp1
fstore: sta tmp2
        iny
        cpy #MAPW
        bne fwd
        ldy #MAPW-1             ; right to left
        lda (ptr1),y
        sta tmp2
        dey
bwd:    lda (ptr1),y
        ldx tmp2
        bpl bstore
        jsr isopen
        bcc bstore
        ora #SEEN
        sta (ptr1),y
        inc tmp1
bstore: sta tmp2
        dey
        bpl bwd
        lda tmp1
        ldx #0
        rts

_FromRow:
        lda _FrCur
        sta ptr1
        lda _FrCur+1
        sta ptr1+1
        lda _FrNb               ; ptr2 = nb, ptr3 = nb - 1, ptr4 = nb + 1
        sta ptr2
        sta ptr3
        sta ptr4
        lda _FrNb+1
        sta ptr2+1
        sta ptr3+1
        sta ptr4+1
        lda ptr3
        bne @1
        dec ptr3+1
@1:     dec ptr3
        inc ptr4
        bne @2
        inc ptr4+1
@2:     lda #0
        sta tmp1
        ldy #0
fr:     lda (ptr1),y
        jsr isopen
        bcc frnext
        sta tmp2
        lda (ptr2),y            ; above / below
        bmi frmark
        cpy #0
        beq frright
        lda (ptr3),y            ; diagonally left
        bmi frmark
frright:
        cpy #MAPW-1
        beq frnext
        lda (ptr4),y            ; diagonally right
        bpl frnext
frmark: lda tmp2
        ora #SEEN
        sta (ptr1),y
        inc tmp1
frnext: iny
        cpy #MAPW
        bne fr
        lda tmp1
        ldx #0
        rts
