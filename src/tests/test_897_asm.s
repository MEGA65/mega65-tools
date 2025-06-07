        .export _test_resource_read

        .p4510

.segment	"CODE"

.proc	_test_resource_read: near
	lda $7f0
	ldx $7f1
	ldy $7f2
	ldz $7f3
	sta $d645 		; Trigger Hypervisor trap
	nop			; CPU delay slot required after any hypervisor trap
	sta $7f4
	stx $7f5
	sty $7f6
	stz $7f7
	php
	pla
	sta $7f8

	;;  Set C function return value to 0
	lda #0
	tax
	
	rts
.endproc

