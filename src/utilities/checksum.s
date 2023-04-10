	.setcpu		"4510"
	.export _checksum_fast
        .export _chks_fast
        .import _chks_size

_checksum_fast:
        php
        pha
        phx
        phy
        phz

        cld

        lda #$00
        tax
        tay
        taz
        neg
        neg
        sta _chks_fast

        lda _chks_size
        and #%00000011
        beq multiple_of_four
        tax
        lda #$c8
        clc
        adc _chks_size+1
        tab

        lda _chks_size
        sta @copy_rest+1
@loop:
        dec @copy_rest+1
@copy_rest:
        lda $00 ; self modifying code! is replaced with remainder addresses
        sta _chks_fast-1,x
        dex
        bne @loop
        lda @copy_rest+1
        bra :+

multiple_of_four:
        lda _chks_size
:       sec
        sbc #$04
        sta _chks_size

start:
        lda #$c8
        tab

        lda #$fc
        ldx _chks_size+1
        bne :+
        lda _chks_size
:       sta add+3

        neg
        neg
        lda _chks_fast
        clc

add:
        ; actual 32 bit checksum loop (looping per page)
        neg
        neg
        adc $00
        dec add+3
        dec add+3
        dec add+3
        dec add+3
        bne add

        ; finally add first 32 bit word
        neg
        neg
        adc $00

        ; next page or done?
        dec _chks_size+1
        bmi done

        ; move to next page and reset quad counter (add+3)
        ; carry needs to stay unchanged until we jump back to add
        pha
        tba
        inc
        tab
        lda _chks_size+1
        beq :+
        lda #$fc
        bra :++
:       lda _chks_size
        beq exact_page_boundary
:       sta add+3
        pla
        bra add

exact_page_boundary:
        pla
done:
        neg
        neg
        sta _chks_fast
        neg
        neg
        sta _chks_fast+4
        adc _chks_fast+2
        tay
        txa
        adc _chks_fast+3
        sta _chks_fast+1
        tya
        adc #$00
        sta _chks_fast
        lda _chks_fast+1
        adc #$00
        sta _chks_fast+1

        lda #$00
        tab

        plz
        ply
        plx
        pla
        plp
        rts

_chks_fast:
        .dword 0
        .dword 0
