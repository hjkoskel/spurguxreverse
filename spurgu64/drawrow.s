; drawrow.s -- inner loop of the map renderer (game.c DrawMap).
; Translates DrN map bytes at DrSrc into screen codes (TS[]) at DrScr and
; colours (TC[]) at DrCol: about 30 cycles per cell instead of ~130 in C.

        .export _DrawRow
        .import _TS, _TC, _DrSrc, _DrScr, _DrCol, _DrN
        .importzp ptr1, ptr2, ptr3

_DrawRow:
        lda _DrSrc
        sta ptr1
        lda _DrSrc+1
        sta ptr1+1
        lda _DrScr
        sta ptr2
        lda _DrScr+1
        sta ptr2+1
        lda _DrCol
        sta ptr3
        lda _DrCol+1
        sta ptr3+1
        ldy #0
loop:   cpy _DrN
        beq done
        lda (ptr1),y
        tax
        lda _TS,x
        sta (ptr2),y
        lda _TC,x
        sta (ptr3),y
        iny
        bne loop
done:   rts
