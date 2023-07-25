        ; void ip_checksum_recv();
	; IP header checksum calculation
        ; Ethernet frame expected at $d800 (IP hdr start at $d810)
        ; Result checksum is returned in AX (A lo, X hi)
        .export _ip_checksum_recv 
        
        .segment        "CODE"

        .p4510
        
.proc   _ip_checksum_recv: near
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
        tax                     ; X = chks hi
        tya                     ; A = chks lo
        rts                     ; return chks (AX)

        .segment        "DATA"
result:
        .dword 0
.endproc
