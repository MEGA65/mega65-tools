	.setcpu		"4510"
	.export _setup_irq
	.import _test_status
	
_setup_irq:
  sei
	;; disable cia irqs
  lda #$7f
	sta $dc0d
	sta $dd0d
  ;; ack cia irqs potentially pending
	lda $dc0d
	lda $dd0d

  ;; disable RSTDELEN
  lda #%01000000
	trb $d05d
	;; raster irq compare
	lda #$60
	sta $d079
  ;; enable physical rasters
  lda #%10000111
	trb $d07a
  ;; enable raster irq
	lda #$01
	sta $d01a
  ;; ack raster irq potentially pending
	lda #$ff
	sta $d019

  ;; setup irq vector
	lda #<_irq_handler
	sta $0314
	lda #>_irq_handler
	sta $0315

  cli
	rts

_irq_handler:
	lda #$ff
	sta $d019

	lda _test_status
	bmi first_frame

	;; current frame counter
	lda $d7fa
	cmp last_frame_counter
	bne exit

	;; we are triggered the second time during current frame_counter -> fail
	lda #$01
	sta _test_status
	jmp exit

first_frame:
	lda #$00
	sta _test_status

exit:
	lda $d7fa
	sta last_frame_counter
	jmp $ea31


last_frame_counter:
  .word $0000
