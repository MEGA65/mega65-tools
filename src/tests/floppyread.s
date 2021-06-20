	.export _readtrackgaps

_readtrackgaps:	

	SEI
	
	;; Backup some ZP space for our 32-bit pointer
	LDX #$00
save:	
	LDA $FA,X
	STA temp,X
	INX
	CPX #4
	BNE save	
	
	;; Initialise 32-bit ZP pointer to bank 5 ($50000)
	LDA #$00
	STA $FA
	STA $FB
	STA $FD
	LDA #$05
	STA $FC
	
waitforindexhigh:
	LDX $D6A0
	BPL waitforindexhigh
waitforfirstindexedge:
	LDX $D6A0
	BMI waitforfirstindexedge
	;; Pre-load value of $D6AC so we can wait for it to change
	LDA $D6AC
	STA $FF
loop:
waitfornextfluxevent:
	LDA $D6AC
	CMP $FF
	BEQ waitfornextfluxevent
	STA $FF			; Update comparison value
	INC $D020
	;; Store real length in bank 5
	LDA $D6A9
	;; STA [$FA],Z
	.byte $EA,$92,$FA	; STA [$FA],Z to save value

	.byte $1B  		; INZ

	LDA $D6AA
	;; STA [$FA],Z
	.byte $EA,$92,$FA	; STA [$FA],Z to save value
	
	;; Show some activity while doing it
	.byte $9C,$10,$C0 	; STZ $C010
	LDY $FB
	STA $C012

	;; Are we done yet?
	;; INZ
	.byte $1B  		; INZ
	BNE loop
	INC $FB
	LDY $FB
	BNE loop

done:
	;;  Done reading track or we already filled 64KB
	
	LDX #$00
restore:	
	LDA temp,X
	STA $FA,X
	INX
	CPX #4
	BNE restore
	
	CLI
	RTS 


temp:	.byte 0,0,0,0
