        .export _test_loop

        .p4510

.segment	"CODE"

.proc	_test_loop: near
        sei

        ; enable VIC-IV
        lda #$47
        sta $d02f
        lda #$53
        sta $d02f
        lda #65
        sta $00

        ; number of frames to iterate
        ldz #120

        ; init frame counter
        ldx $d7fa

loop:
        ; wait for tx ready
        lda $d6e0
        bpl loop

        ; set frame size 256 bytes
        lda #$ff
        sta $d6e2
        lda #0
        sta $d6e3

        ; tx trigger
        lda #1
        sta $d6e4

        ldy #$60
:       lda #$00

        ; main test: sbc still calculating correctly?
        sec
        sbc #$80
        cmp #$80
        bne error
        dey
        bne :-

        ; check for change in frame counter
        cpx $d7fa
        beq loop
        ldx $d7fa
        ; decrease counter (z = num frames to go)
        dez
        bne loop

        ; return code 1
        lda #$01
        ldx #$00
        cli
        rts

error:
        ; return code 0
        lda #$00
        tax
        cli
        rts

.endproc
