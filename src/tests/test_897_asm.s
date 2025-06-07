        .export _test_resource_read

        .p4510

.segment	"CODE"

.proc	_test_resource_read: near
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

