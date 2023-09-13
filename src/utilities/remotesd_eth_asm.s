        ; void fastcall dma_copy_eth_io(void *src, void *dst, uint16_t size);
        ; dma copy with eth I/O personality enabled
        .export _dma_copy_eth_io

        ; uint8_t fastcall cmp_c000_c200()
        ; compare 0xc000-0xc1ff with 0xc200-0xc3ff
        ; return 0 if equal, 1 if not equal
        .export _cmp_c000_c200
        .export _cmp_c000_c800

        .p02
        .include        "zeropage.inc"
        .p4510
        .import incsp4

        .segment        "CODE"

.proc   _dma_copy_eth_io: near
        .p02
        sta dma_copy_count
        stx dma_copy_count+1
        ldy #$00
        lda (sp),y
        sta dma_copy_dst
        iny
        lda (sp),y
        sta dma_copy_dst+1
        iny
        lda (sp),y
        sta dma_copy_src
        iny
        lda (sp),y
        sta dma_copy_src+1
        
        .p4510
        lda #$45
        sta $d02f 
        lda #$54
        sta $d02f 

	sta $d707
	.byte $00      ; end of job options
	.byte $00      ; copy
dma_copy_count:
	.word $0000    ; count
dma_copy_src:
	.word $0000    ; src
	.byte $80      ; src bank
dma_copy_dst:
	.word $0000    ; dst
	.byte $80      ; dst bank
	.byte $00      ; cmd hi
	.word $0000    ; modulo / ignored

        jmp incsp4
.endproc

.proc  _cmp_c000_c200: near
        lda #$c0
        tab
        ldx #$00
loop1:
        lda $c200,x
        cmp $00,x
        bne not_equal
        iny
        bne loop1
        lda #$c1
        tab
loop2:
        lda $c300,x
        cmp $00,x
        bne not_equal
        iny
        bne loop2
        lda #$00
        tab
        tax
        rts        
not_equal:
        lda #$00
        tab
        tax
        lda #$01
        rts
.endproc

.proc  _cmp_c000_c800: near
        lda #$c0
        tab
        ldx #$00
loop1:
        lda $c822,x
        cmp $00,x
        bne not_equal
        iny
        bne loop1
        lda #$c1
        tab
loop2:
        lda $c922,x
        cmp $00,x
        bne not_equal
        iny
        bne loop2
        lda #$00
        tab
        tax
        rts        
not_equal:
        lda #$00
        tab
        tax
        lda #$01
        rts
.endproc
