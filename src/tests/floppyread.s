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
	
	;; Initialise 32-bit ZP pointer to bank 4 ($40000)
	LDA #$00
	STA $FA
	STA $FB
	STA $FD
	LDA #$04
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
	LDX $D6A0
	BMI done
waitfornextfluxevent:
	LDA $D6AC
	CMP $FF
	BEQ waitfornextfluxevent
	INC $D020
	;; Store byte in bank 4	
	;; STA [$FA],Z
	.byte $EA,$92,$FA
	
	;; INZ
	.byte $1B  		; INZ
	
	BNE loop
	INC $FB
	BNE loop

done:
	;;  Done reading track or we already filled 64KB
	
	LDX #$00
restore:	
	LDA temp,X
	STA $FA,X
	INX
	CPX #4
	BNE save
	
	CLI
	RTS 


temp:	.byte 0,0,0,0
