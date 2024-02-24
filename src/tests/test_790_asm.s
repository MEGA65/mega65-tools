        .export _test_simple_rts, _test_rts_with_param, _test_rts_extended_stack

        .p4510

.segment	"CODE"

.proc	_test_simple_rts: near
		jsr sub
		lda #0
		tax
		rts
sub:
		rtn #$00

		pla
		pla
		lda #1
		ldx #0
		rts
.endproc

.proc	_test_rts_with_param: near
		sei
		sta sub+1
		sta add+1

		ldy #$40
		sty expected+1
		tys
		tsx
		stx saved_stack_ptr
		txa
		clc
add:
		adc #$00
		sta expected

		jsr sub

		tsx
		cpx expected
		bne error
		tsy
		cpy expected+1
		bne error
		lda #0
		bra done
sub:
		rtn #$00
		inc $d020
		bra *
error:
		lda #1
done:
		ldy #1
		tys
		ldx saved_stack_ptr
		txs
		ldx #0
		cli
		rts
expected:
		.byte $00, $00
saved_stack_ptr:
		.byte $00
.endproc

.proc	_test_rts_extended_stack: near
		sei

		cle
		sta increment
		lda original_stack
		clc
		adc increment
		sta result
		lda #0
		adc original_stack+1
		sta result+1
		lda increment
		sta sub+1

		; save the stack pointer
		tsx
		txa
		taz
		ldx original_stack
		ldy original_stack+1
		txs
		tys

		jsr sub

		tsx
		cpx result
		bne error
		tsy
		cpy result+1
		bne error
		
		tza
		tax
		ldy #1
		txs
		tys
		see

		ldy #0
		bra done
sub:
		rtn #$01
error:
		ldy #1
done:
		tza
		tax
		tya
		ldy #1
		txs
		tys
		see

		ldx #0
		ldz #0
		cli
		rts
original_stack:
		.byte $f0, $40
result:
		.byte $f0, $40
increment:
		.byte 0
.endproc

