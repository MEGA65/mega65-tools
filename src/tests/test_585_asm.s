	.setcpu "4510"
	.export _test_memory
	.import _test_status
	farptr = $a5
	scrnptr = $24

	;; destructive memory test
	;; set 32bit start address to $a5-$a8
	;; tests until lo word reads $1000
_test_memory:
	lda #$ff	;; set test_status to -1 (fail)
	sta _test_status
	ldz #0
tm_loop0:
	ldy #$00
	lda farptr+2
	jsr tm_printByte
tm_loop1:
	ldy #$02
	lda farptr+1
	jsr tm_printByte
tm_loop2:
	ldy #$04
	tza
	jsr tm_printByte
	ldx #3
tm_loop_val:
	lda test_val,x
	nop
	sta (farptr),z

	pha

	;; Bust cache
	phz
	tza
	eor #$80
	and #$80
	taz
	nop
	lda (farptr),z
	plz

	pla
	
	nop
	cmp (farptr),z
	bne tm_fail	;; fail
	dex
	bpl tm_loop_val
	inz
	bne tm_loop2

	inc farptr+1
	bne tm_loop1

	inc farptr+2
	lda farptr+2
	and #$0f
	bne tm_loop0

	lda #0		;; success!
	sta _test_status
	rts

tm_fail:
	stz farptr
	rts

	;; test pattern (alternating bit sets)
test_val:
	.byte $00, $ff, $55, $aa

tm_printByte:
	tax
	lsr
	lsr
	lsr
	lsr
	cmp #$0a
	bcc tm_pp_dig_num2
	sec
	sbc #$09	;; subtract 9 -> a-f get 01-06
	bra tm_pp_dig2
tm_pp_dig_num2:
	ora #$30
tm_pp_dig2:
	sta (scrnptr),y
	iny
	txa
	and #$0f
	cmp #$0a
	bcc tm_pp_dig_num3
	sec
	sbc #$09	;; subtract 9 -> a-f get 01-06
	bra tm_pp_dig3
tm_pp_dig_num3:
	ora #$30
tm_pp_dig3:
	sta (scrnptr),y
	rts
