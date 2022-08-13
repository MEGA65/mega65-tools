;;
;; Test for MAPed DMA list calls with $D706
;;
;; Executing a DMA job using $D706 as trigger should use the MAPed memory
;; context instead of flat memory
;;

	.setcpu		"4510"
	.feature	leading_dot_in_identifiers, loose_string_term



ADDR_RELOC	=	$A000
TARGETVAL	=	$DE

;===========================================================
;BASIC interface
;-----------------------------------------------------------
	.code
;start 2 before load address so
;we can inject it into the binary
	.org		$07FF			
						
	.byte		$01, $08		;load address
	
;BASIC next addr and this line #
	.word		_basNext, $000A		
	.byte		$9E			;SYS command
	.asciiz		"2061"			;2061 and line end
_basNext:
	.word		$0000			;BASIC prog terminator
	.assert		* = $080D, error, "BASIC Loader incorrect!"
;-----------------------------------------------------------
bootstrap:
		JMP	init
;===========================================================


	.include	"unittestlog.s"

strTestName0:
		.asciiz	"MAPed DMA $D706"

strTestSuccess0:
		.asciiz	"DMA call succeeded"
strTestError0:
		.asciiz	"DMA call failed"
strTestErrorEnd:

valErrorCode:
		.byte	$00

valTargetData:
		.res	16, 0

;-----------------------------------------------------------
init:
;-----------------------------------------------------------
;	disable standard CIA irqs
		LDA	#$7F			
		STA	$DC0D		;CIA IRQ control

		SEI

;	Bank out BASIC (keep Kernal and IO).  First, make sure that the IO port
;	is set to output on those lines.
		LDA	$00
		ORA	#$07
		STA	$00
		
;	Now, exclude BASIC from the memory map (and include Kernal and IO)
		LDA	$01
		AND	#$FE
		ORA	#$06
		STA	$01

		LDA	#$00
		LDX	#$00
		LDY	#$00
		LDZ	#$00

		MAP
;		EOM

		JSR	initM65IOFast

		LDA	#<strTestName0
		LDX	#>strTestName0
		LDY	#<454
		LDZ	#>454
		JSR	unit_test_setup

		JSR	copyFarProgram

		LDA	#$00
		LDX	#$00
		LDY	#$00
		LDZ	#$24
;		LDZ	#$00

		MAP
;		EOM

		LDA	#'1'
		STA	valErrorCode

		LDA	ADDR_RELOC
		CMP	FARPROGRAM
		BNE	@fail

		JSR	farRoutine

		LDA	#$00
		LDX	#$00
		LDY	#$00
		LDZ	#$00

		MAP
;		EOM

		LDA	#'2'
		STA	valErrorCode

		LDA	valTargetData
		CMP	#TARGETVAL
		BNE	@fail

		LDA	#$05
		STA	$D020

		LDA	#<strTestSuccess0
		LDX	#>strTestSuccess0
		JSR	unit_test_ok

		JMP	main

@fail:
		LDA	valErrorCode
		STA	$0400

		STA	strTestErrorEnd - 2

		LDA	#<strTestError0
		LDX	#>strTestError0
		JSR	unit_test_fail

		LDA	#$02
		STA	$D020

main:
		LDY	#0
		JSR	unit_test_done


halt:
		JMP	halt


;-----------------------------------------------------------
initM65IOFast:
;-----------------------------------------------------------
;	Go fast, first attempt
		LDA	#65
		STA	$00

;	Enable M65 enhanced registers
		LDA	#$47
		STA	$D02F
		LDA	#$53
		STA	$D02F
;	Switch to fast mode, be sure
; 	1. C65 fast-mode enable
		LDA	$D031
		ORA	#$40
		STA	$D031
; 	2. MEGA65 40.5MHz enable (requires C65 or C128 fast mode 
;	to truly enable, hence the above)
		LDA	#$40
		TSB	$D054
		
		RTS


dmaNearFillList:
	.byte	$0B  				; Request format is F018B
	.byte	$80,$00 			; Source MB 
	.byte	$81,$00 			; Destination MB 
	.byte	$00  				; No more options
		
	.byte	$00					;Command LSB
	.word	ENDFARPROGRAM - ADDR_RELOC				;Count LSB Count MSB
dmaNearFillSrc:
	.word	FARPROGRAM			;Source Address LSB Source Address MSB
	.byte	$00					;Source Address BANK and FLAGS
dmaNearFillDst:
	.word	ADDR_RELOC			;Destination Address LSB Destination Address MSB
	.byte	$04					;Destination Address BANK and FLAGS
	.byte	$00					;Command MSB
	.word	$0000				;Modulo LSB / Mode Modulo MSB / Mode


;-----------------------------------------------------------
copyFarProgram:
;-----------------------------------------------------------
		LDA	#$00
		STA	$D702
		LDA	#>dmaNearFillList
		STA	$D701
		LDA	#<dmaNearFillList
		STA	$D706

		RTS


;===========================================================
FARPROGRAM:
;===========================================================
	.org		ADDR_RELOC
;===========================================================

;-----------------------------------------------------------
farRoutine:
;-----------------------------------------------------------
		LDA	#$00
		STA	$D702
		LDA	#>dmaFarFillList
		STA	$D701
		LDA	#<dmaFarFillList
		STA	$D706

		RTS

dmaFarFillList:
	.byte	$0B  				; Request format is F018B
	.byte	$80,$00 			; Source MB 
	.byte	$81,$00 			; Destination MB 
	.byte	$00  				; No more options
		
	.byte	$03					;Command LSB
	.word	$0016				;Count LSB Count MSB
dmaFarFillSrc:
	.word	TARGETVAL			;Source Address LSB Source Address MSB
	.byte	$00					;Source Address BANK and FLAGS
dmaFarFillDst:
	.word	valTargetData		;Destination Address LSB Destination Address MSB
	.byte	$00					;Destination Address BANK and FLAGS
	.byte	$00					;Command MSB
	.word	$0000				;Modulo LSB / Mode Modulo MSB / Mode

;===========================================================
ENDFARPROGRAM:
;===========================================================