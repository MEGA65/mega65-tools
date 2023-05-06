        ; void ip_checksum_recv();
	; IP header checksum calculation
        ; Ethernet frame expected at $d800 (IP hdr start at $d810)
        ; Result checksum is returned in AX (A lo, X hi)
        .export _ip_checksum_recv 
        
        ; void fastcall dma_copy_eth_io(void *src, void *dst, uint16_t size);
        ; dma copy with eth I/O personality enabled
        .export _dma_copy_eth_io

        .include        "zeropage.inc"
        .p4510

        .segment        "CODE"

_dma_copy_eth_io:
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

        lda sp
        clc
        adc #$04
        sta sp
        .p4510

        rts

_ip_checksum_recv:
        lda #$45
        sta $d02f 
        lda #$54
        sta $d02f 
        lda #$d8
        tab
        clc
        neg
        neg
        lda $10
        neg
        neg
        adc $14
        neg
        neg
        adc $18
        neg
        neg
        adc $1c
        neg
        neg
        adc $20

        neg
        neg
        sta result              ; chks lo = result[0] + result[2]
        adc result+2
        tay                     ; Y = chks lo
        lda #$00                ; A is free now, use it to
        tab                     ; reset bp to zp
        txa
        adc result+3            ; chks hi = result[1] + result[3]
        bcc :+                  ; carry still set?
        iny                     ; increase chks lo
        bne :+                  ; overflow?
        inc                     ; increase chks hi
        bne :+                  ; still carry?
        iny                     ; increase chks lo again
:
        tax                     ; keep in x as return value
        tya                     ; A = chks lo
        rts                     ; return chks (AX)

        .segment        "DATA"
result:
        .dword 0
