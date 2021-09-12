
	* = $0801
basicheader:
	.word $80a,2020
	.byte $9e,$32,$30,$36,$31,0,0,0

	
main:	
	sei
	lda #$47
	sta $D02f
	lda #$53
	sta $d02f
	lda #65
	sta 0

keywait:
	inc $d020
	lda $d610
	cmp #$20
	beq start
	bne keywait
	cmp #0
	beq keywait
	sta $d610
	jmp keywait
	
start:
	inc $0400
	// Copy load routine down to tape buffer, just so we don't crash after loading
	ldx #$3f
loopy:	
	lda loadroutine,x
	sta $0380,x
	dex
	bpl loopy
	jmp $0380

loadroutine:	
	// Call dos_setname()
	// Filename MUST be on a page boundary!
	ldy #>filename
	lda #$2E     		// dos_setname Hypervisor trap
	sta $D640		// Do hypervisor trap
	nop			// Wasted instruction slot required following hyper trap instruction
	// XXX Check for error (carry would be clear)

	// Get Load address into $00ZZYYXX
	// We load over the screen so it is visibly obvious
	ldx #$00
	ldy #$00
	// ldz #$00
	.byte $a3,$00

	// Ask hypervisor to do the load
	// $36 for chip RAM at $00ZZYYXX
	// $3E for Attic RAM at $8000000 + $00ZZYYXX
	lda #$3e
	sta $D640		
	nop
	// XXX Check for error (carry would be clear)

	// ldz #$00
	.byte $a3,$00

	clc
end:	
	inc $d020
	bcc end

	.align $100
filename:
	.text "MEGA65.ROM"
	.byte 0
	
