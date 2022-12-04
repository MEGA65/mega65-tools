	;; A simple IRQ-able fastloader for the MEGA65 for reading from the
	;; internal drive or a disk image.

	;; XXX - Doesn't seek to tracks yet, so will not work with real disks
	;; until implemented. Won't be hard to implement.

	!byte $00,$c0
	*=$c000
	
	
program_start:	

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

	lda #$01
	sta $0286
	jsr $e544
	
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

	lda #$16
	sta $d018
	
	lda #<irq_handler
	sta $0314
	lda #>irq_handler
	sta $0315
	cli

	;; Example for using the fast loader
	
	;; copy filename from start of screen
	;; Expected to be PETSCII and $A0 padded at end, and exactly 16 chars
	ldx #$0f
	lda #$a0
clearfilename:
	sta fastload_filename,x
	dex
	bpl clearfilename
	ldx #$ff
filenamecopyloop:
	inx
	cpx #$10
	beq endofname
	lda filename,x
	beq endofname
	sta fastload_filename,x
	bne filenamecopyloop
endofname:	
	inx
	stx fastload_filename_len
	
	;; Set load address (32-bit)
	;; = $07ff ($0801 - 2 bytes for BASIC header)
	lda #$ff
	sta fastload_address+0
	lda #$07
	sta fastload_address+1
	lda #$00
	sta fastload_address+2
	lda #$00
	sta fastload_address+3

	;; Give the fastload time to get itself sorted
	;; (largely seeking to track 0)
wait_for_fastload:	
	lda fastload_request
	bne wait_for_fastload
	
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
	beq done
	
error
	inc $d020
	jmp error

done:
	;; Clear basic header from loadaddr - 2
	lda #0
	sta $7ff
	sta $800

	;; Restore IRQ and return to basic
	sei
	lda #$31
	sta $0314
	lda #$ea
	sta $0315
	;; Disable raster IRQ
	lda #$00
	sta $d01a
	;; Re-enable CIA interrupt
	lda #$81
	sta $dc0d
	cli

	;; Revert to 1MHz
	lda #64
	sta 0
	
	rts
	
	
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

filename:
	;; AMIGA-BALL for testing
	!byte $41,$4d,$49,$47,$41,$2d,$42,$41
	!byte $4c,$4c,$a0,$a0,$a0,$a0,$a0,$a0

filename_foo:
	;; GYRRUS for testing
	!byte $47,$59,$52,$52,$55,$53,$a0,$a0 
	!byte $a0,$a0,$a0,$a0,$a0,$a0,$a0,$a0

	
;; ----------------------------------------------------------------------------
;; ----------------------------------------------------------------------------
;; ----------------------------------------------------------------------------
	
	;; ------------------------------------------------------------
	;; Actual fast-loader code
	;; ------------------------------------------------------------
fastload_filename:	
	*=*+16
fastload_filename_len:	
	!byte 0
fastload_address:	
	!byte 0,0,0,0
fastload_request:	
	;; Start with seeking to track 0
	!byte 4
	;; Remember the state that requested a sector read
fastload_request_stashed:	
	!byte 0

fl_current_track:	!byte 0
	
fl_file_next_track:	!byte 0
fl_file_next_sector:	!byte 0
prev_track:	!byte 0
prev_sector:	!byte 0
prev_side:	!byte 0
	
fastload_sector_buffer:
	*=*+512
	
fastload_irq:
	;; If the FDC is busy, do nothing, as we can't progress.
	;; This really simplifies the state machine into a series of
	;; sector reads
	lda fastload_request
	bne todo
	rts
todo:	
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
	cmp #6
	bcc fl_job_ok
	;; Ignore request/status codes that don't correspond to actions
	rts
fl_job_ok:	
	asl
	tax
	jmp (fl_jumptable,x)
	
fl_jumptable:
	!16 fl_idle
	!16 fl_new_request
	!16 fl_directory_scan
	!16 fl_read_file_block
	!16 fl_seek_track_0
	!16 fl_reading_sector

fl_idle:
	rts

fl_seek_track_0:
	lda $d082
	and #$01
	beq fl_not_on_track_0
	lda #$00
	sta fastload_request
	sta fl_current_track
	rts
fl_not_on_track_0:
	;; Step back towards track 0
	lda #$10
	sta $d081
	rts

fl_select_side1:	
	lda #$01
	sta $d086 		; requested side
	;; Sides are inverted on the 1581
	lda #$60
	sta $d080 		; physical side selected of mechanical drive
	rts

fl_select_side0:	
	lda #$00
	sta $d086 		; requested side
	;; Sides are inverted on the 1581
	lda #$68
	sta $d080 		; physical side selected of mechanical drive
	rts
	
	
fl_new_request:
	;; Acknowledge fastload request
	lda #2
	sta fastload_request
	;; Start motor
	lda #$60
	sta $d080
	;; Request T40 S3 to start directory scan
	;; (remember we have to do silly translation to real sectors)
	lda #40-1
	sta $d084
	lda #(3/2)+1
	sta $d085
	jsr fl_select_side0

	;; Request read
	jsr fl_read_sector
	rts
	
fl_directory_scan:
	
	;; Check if our filename we want is in this sector
	jsr fl_copy_sector_to_buffer

	;; (XXX we scan the last BAM sector as well, to keep the code simple.)
	;; filenames are at offset 4 in each 32-byte directory entry, padded at
	;; the end with $A0
	lda #<fastload_sector_buffer
	sta fl_buffaddr+1
	lda #>fastload_sector_buffer
	sta fl_buffaddr+2

fl_check_logical_sector:
	ldx #$05
fl_filenamecheckloop:
	ldy #$00

fl_check_loop_inner:

fl_buffaddr:
	lda fastload_sector_buffer+$100,x
	
	cmp fastload_filename,y	
	bne fl_filename_differs
	inx
	iny
	cpy #$10
	bne fl_check_loop_inner

	;; Filename matches
fl_found_file:	
	txa
	sec
	sbc #$12
	tax
	lda fl_buffaddr+2
	cmp #>fastload_sector_buffer
	bne fl_file_in_2nd_logical_sector
	;; Y=Track, A=Sector
	lda fastload_sector_buffer,x
	tay
	lda fastload_sector_buffer+1,x
	jmp fl_got_file_track_and_sector
fl_file_in_2nd_logical_sector:
	;; Y=Track, A=Sector
	lda fastload_sector_buffer+$100,x
	tay
	lda fastload_sector_buffer+$101,x
fl_got_file_track_and_sector:
	;; Store track and sector of file
	sty fl_file_next_track
	sta fl_file_next_sector

	;; Advance to next state
	lda #3
	sta fastload_request
	
	;; Request reading of next track and sector
	jsr fl_read_next_sector
	rts
	
fl_filename_differs:
	;; Skip same number of chars as though we had matched
	cpy #$10
	beq fl_end_of_name
	inx
	iny
	jmp fl_filename_differs
fl_end_of_name:
	;; Advance to next directory entry
	txa
	clc
	adc #$10
	tax
	bcc fl_filenamecheckloop
	inc fl_buffaddr+2
	lda fl_buffaddr+2
	cmp #(>fastload_sector_buffer)+1
	bne fl_checked_both_halves
	jmp fl_check_logical_sector
fl_checked_both_halves:	
	
	;; No matching name in this 512 byte sector.
	;; Load the next one, or give up the search
	inc $d085
	lda $d085
	cmp #11
	bne fl_load_next_dir_sector
	;; Ran out of sectors in directory track
	;; (XXX only checks side 0, and assumes DD disk)

	;; Mark load as failed
	lda #$80 		; $80 = File not found
	sta fastload_request	
	rts

fl_load_next_dir_sector:	
	;; Request read
	jsr fl_read_sector
	;; No need to change state
	rts

fl_read_sector:
	;; Remember the state that we need to return to
	lda fastload_request
	sta fastload_request_stashed
	;; and then set ourselves to the track stepping/sector reading state
	lda #5
	sta fastload_request
	;; FALLTHROUGH
	
fl_reading_sector:
	;; Check if we are already on the correct track/side
	;; and if not, select/step as required

	lda fl_current_track
	lda $d084
	cmp fl_current_track
	beq fl_on_correct_track
	bcc fl_step_in
fl_step_out:
	;; We need to step first
	lda #$18
	sta $d081
	inc fl_current_track
	rts
fl_step_in:
	;; We need to step first
	lda #$10
	sta $d081
	dec fl_current_track
	rts
	
fl_on_correct_track:

	lda $d084
	cmp prev_track
	bne fl_not_prev_sector
	lda $d086
	cmp prev_side
	bne fl_not_prev_sector
	lda $d085
	cmp prev_sector
	bne fl_not_prev_sector

	;; We are being asked to read the sector we already have in the buffer
	;; Jump immediately to the correct routine
	lda fastload_request_stashed
	sta fastload_request
	jmp fl_fdc_not_busy

fl_not_prev_sector:	
	lda #$40
	sta $d081

	;; Now that we are finally reading the sector,
	;; restore the stashed state ID
	lda fastload_request_stashed
	sta fastload_request

	rts

fl_step_track:
	lda #3
	sta fastload_request
	;; FALL THROUGH
	
fl_read_next_sector:
	;; Check if we reached the end of the file first
	lda fl_file_next_track
	bne fl_not_end_of_file
	rts
fl_not_end_of_file:	
	;; Read next sector of file	
	jsr fl_logical_to_physical_sector
	jsr fl_read_sector
	rts

	
fl_logical_to_physical_sector:

	;; Remember current loaded sector, so that we can optimise when asked
	;; to read other half of same physical sector
	lda $d084
	sta prev_track
	lda $d085
	sta prev_sector
	lda $d086
	sta prev_side
	
	;; Convert 1581 sector numbers to physical ones on the disk.
	;; Track = Track - 1
	;; Sector = 1 + (Sector/2)
	;; Side = 0
	;; If sector > 10, then sector=sector-10, side=1
	;; but sides are inverted
	jsr fl_select_side0
	lda fl_file_next_track
	dec
	sta $d084
	lda fl_file_next_sector
	lsr
	inc
	cmp #11
	bcs fl_on_second_side
	sta $d085
	rts
	
fl_on_second_side:
	sec
	sbc #10
	sta $d085
	jsr fl_select_side1
	rts
	
	
fl_read_file_block:
	;; We have a sector from the floppy drive.
	;; Work out which half and how many bytes,
	;; and copy them into place.

	;; Get sector from FDC
	jsr fl_copy_sector_to_buffer

	;; Assume full sector initially
	lda #254
	sta fl_bytes_to_copy
	
	;; Work out which half we care about
	lda fl_file_next_sector
	and #$01
	bne fl_read_from_second_half
fl_read_from_first_half:
	lda #(>fastload_sector_buffer)+0
	sta fl_read_dma_page

	lda fastload_sector_buffer+1
	sta fl_file_next_sector
	lda fastload_sector_buffer+0
	sta fl_file_next_track
	bne fl_1st_half_full_sector
fl_1st_half_partial_sector:
	lda fastload_sector_buffer+1
	sta fl_bytes_to_copy	
	;; Mark end of loading
	lda #$00
	sta fastload_request
fl_1st_half_full_sector:
	jmp fl_dma_read_bytes
	
fl_read_from_second_half:
	lda #(>fastload_sector_buffer)+1
	sta fl_read_dma_page
	lda fastload_sector_buffer+$101
	sta fl_file_next_sector
	lda fastload_sector_buffer+$100
	sta fl_file_next_track
	bne fl_2nd_half_full_sector
fl_2nd_half_partial_sector:
	lda fastload_sector_buffer+$101
	sta fl_bytes_to_copy
	;; Mark end of loading
	lda #$00
	sta fastload_request
fl_2nd_half_full_sector:
	;; FALLTHROUGH
fl_dma_read_bytes:

	;; Update destination address
	lda fastload_address+3
	asl
	asl
	asl
	asl
	sta fl_data_read_dmalist+2
	lda fastload_address+2
	lsr
	lsr
	lsr
	lsr
	ora fl_data_read_dmalist+2
	sta fl_data_read_dmalist+2
	lda fastload_address+2
	and #$0f
	sta fl_data_read_dmalist+12
	lda fastload_address+1
	sta fl_data_read_dmalist+11
	lda fastload_address+0
	sta fl_data_read_dmalist+10

	;; Copy FDC data to our buffer
	lda #$00
	sta $d704
	lda #>fl_data_read_dmalist
	sta $d701
	lda #<fl_data_read_dmalist
	sta $d705

	;; Update load address
	lda fastload_address+0
	clc
	adc fl_bytes_to_copy
	sta fastload_address+0
	lda fastload_address+1
	adc #0
	sta fastload_address+1
	lda fastload_address+2
	adc #0
	sta fastload_address+2
	lda fastload_address+3
	adc #0
	sta fastload_address+3
	
	;; Schedule reading of next block
	jsr fl_read_next_sector
	
	rts

fl_data_read_dmalist:
	!byte $0b	  ; F011A type list
	!byte $81,$00	  ; Destination MB
	!byte 0 		; no more options
	!byte 0			; copy
fl_bytes_to_copy:	
	!word 0	   		; size of copy
fl_read_page_word:	
fl_read_dma_page = fl_read_page_word + 1
	;; +2 is to skip track/header link
	!word fastload_sector_buffer+2	; Source address
	!byte $00		; Source bank
	
	!word 0			     ; Dest address
	!byte $00		     ; Dest bank
	
	!byte $00		     ; sub-command
	!word 0			     ; modulo (unused)
	
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
