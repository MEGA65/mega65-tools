
basic_header
	;; Auto-detect C64/C65 mode and either way jump into the assembly
	!byte 0x10,0x08,<2021,>2021,0x9e
	!pet "2061"
	!byte 0x00,0x00,0x00
	

program_start
	;; Select MEGA65 IO mode
	lda #$47
	sta $d02f
	lda #$53
	sta $d02f

	;; Select 40MHz mode
	lda #65
	sta $0

	lda #$00
	sta $d020
	sta $d021
	
	;; Install our raster IRQ with our fastloader
	sei

	lda #$7f
	sta $dc0d
	sta $dd0d
	lda #$40
	sta $d012
	lda #$1b
	sta $d011
	lda #$01
	sta $d01a
	dec $d019
	
	lda #<irq_handler
	sta $0314
	lda #>irq_handler
	sta $0315
	cli

	;; Example for using the fast loader
	
	;; copy filename from start of screen
	;; Expected to be PETSCIIZ at $0400
	ldx #$ff
filenamecopyloop:
	inx
	cpx #$10
	beq too_long
	lda $0400,x
	sta fastload_filename,x
	bne filenamecopyloop
too_long:
	inx
	stx fastload_filename_len
	
	;; Set load address (32-bit)
	;; = $40000 = BANK 4
	lda #$00
	sta fastload_address+0
	lda #$00
	sta fastload_address+1
	lda #$04
	sta fastload_address+2
	lda #$00
	sta fastload_address+3

	;; Request fastload job
	lda #$01
	sta fastload_request

	;; Then just wait for the request byte to
	;; go back to $00, or to report an error by having the MSB
	;; set. The request value will continually update based on the
	;; state of the loading.
waiting
	lda fastload_request
	bmi error
	bne waiting

error
	inc $042f
	jmp error

done
	inc $d020
	jmp done

irq_handler
	;; Here is our nice minimalistic IRQ handler that calls the fastload IRQ
	
	dec $d019

	;; Call fastload and show raster time used in the loader
	lda #$01
	sta $d020
	jsr fastload_irq
	lda #$00
	sta $d020

	;; Chain to KERNAL IRQ exit
	jmp $ea81
	
	;; ------------------------------------------------------------
	;; Actual fast-loader code
fastload_filename
	*=*+16
fastload_filename_len
	!byte 0
fastload_address
	!byte 0,0,0,0
fastload_request
	!byte 0

fastload_sector_buffer = $0450
	
fastload_irq:
	;; If the FDC is busy, do nothing, as we can't progress.
	;; This really simplifies the state machine into a series of
	;; sector reads
	inc $0400
	lda $d082
	bpl fl_fdc_not_busy
	rts
fl_fdc_not_busy:	
	;; FDC is not busy, so check what state we are in
	lda fastload_request
	bpl fl_not_in_error_state
	rts
fl_not_in_error_state:
	;; Shift state left one bit, so that we can use it as a lookup
	;; into a jump table.
	;; Everything else is handled by the jump table
	asl
	tax
	jmp (fl_jumptable,x)
	
fl_jumptable:
	!16 fl_idle
	!16 fl_new_request
	!16 fl_directory_scan

fl_idle:
	rts
fl_new_request:
	;; Acknowledge fastload request
	inc fastload_request
	;; Start motor
	lda #$60
	sta $d080
	;; Request T40 S3 to start directory scan
	;; (remember we have to do silly translation to real sectors)
	lda #40-1
	sta $d084
	lda #(3/2)+1
	sta $d085
	lda #$00
	sta $d086 		; side
	;; Request read
	lda #$40
	sta $d081
	rts
fl_directory_scan:
	;; Check if our filename we want is in this sector
	inc $0401
	jsr fl_copy_sector_to_buffer

	;; Check first logical sector
	;; (XXX we scan the last BAM sector as well, to keep the code simple.)
	

	
	rts


fl_copy_sector_to_buffer:
	;; Make sure FDC sector buffer is selected
	lda #$80
	trb $d689

	;; Copy FDC data to our buffer
	lda #$00
	sta $d704
	lda #>fl_sector_read_dmalist
	sta $d701
	lda #<fl_sector_read_dmalist
	sta $d705
	rts

fl_sector_read_dmalist:
	!byte $0b	  ; F011A type list
	!byte $80,$ff	    	; MB of FDC sector buffer address ($FFD6C00)
	!byte 0 		; no more options
	!byte 0			; copy
	!word 512		; size of copy
	!word $6c00		; low 16 bits of FDC sector buffer address
	!byte $0d		; next 4 bits of FDC sector buffer address
	!word fastload_sector_buffer ; Dest address	
	!byte $00		     ; Dest bank
	!byte $00		     ; sub-command
	!word 0			     ; modulo (unused)
