	.setcpu "4510"
	.export _test_memory
	.import _test_status
	farptr = $a5
	scrnptr = $24

	;; destructive memory test
	;; set 32bit start address to $a5-$a8
	;; tests until lo word reads $1000
_test_memory:
	lda #$00	;; set test_status to 0 errors
	sta _test_status
	ldz #0
tm_loop:
	ldx #3
tm_loop_val:
	lda test_val,x
	nop
	sta (farptr),z
	nop
	cmp (farptr),z
	bne tm_fail	;; fail
	dex
	bpl tm_loop_val
tm_next:
	inz
    cpz #$80
	bne tm_loop
	rts

tm_fail:
    inc _test_status
    bra tm_next

	;; test pattern (alternating bit sets)
test_val:
	.byte $00, $ff, $55, $aa
