//
// Refactored fastloader based on code from MEGASISTERS sourcecode.
// This one works with the KickAss assembler.
//
// Include this file in your project, #import "fast_loader_kickass.asm"
// Then you can use the LoadFile(destination,filename) macro: 
// Example: LoadFile($10000, "MYFILE")
// 
// Original code by MegaStyle. Refactored by Trap/Bonzai.
//
// 2022.01.17 * Trap * Initial version 

// ----------------------------------------------------------------------------

//Location of loader buffer
.const FILE_BUFFER = $0200

// ----------------------------------------------------------------------------

.macro LoadFile(addr, fname) {
                    lda #>FLOPPYIO.ZPBase
                    tab // set new zp base
                    bra !+
FileName:
                    .text fname
                    .byte $00
!:
                    lda #addr >> 20
                    sta FLOPPYIO.MBank+1
                    lda #<addr
                    ldx #>addr
                    ldy #( addr & $f0000 ) >> 16
                    jsr FLOPPYIO.SetLoadAddress
                    ldx #<FileName
                    ldy #>FileName
                    jsr FLOPPYIO.LoadFile
                    bcc LoadOK
error:              jmp FLOPPYIO.Error
LoadOK:             tba // restore zp base
// ki-bo: This does not restore the base page to its original value. You may want to consider something like
//
// lda RestoreBP:#$00
// tab
//
//
// and store BP at the beginning with
//
// tba
// sta RestoreBP

}

// ----------------------------------------------------------------------------

.namespace FLOPPYIO {

.align $100
// ki-bo: You could use an alignment with a smaller power of two ($10), since this is just to prevent the data
// from crossing a page boundary. This may help keeping number of unused bytes low.
ZPBase:
// ki-bo: Use .zp directive of kickass to mark this section. The assembler will then automatically address them
// with their low-byte only.
FileNamePtr:        .word $0000
BufferPtr:          .word $0000
NextTrack:          .byte $00
NextSector:         .byte $00
PotentialTrack:     .byte $00
PotentialSector:    .byte $00
SectorHalf:         .byte $00

// -----------------

                    // Error during loading
Error:              inc $d020
                    ldx #0
showerr:            lda errortext,x
                    sta $0800,x
                    inx 
                    cpx #11
                    bne showerr
                    jmp Error
errortext:          .text "load error!"

// -----------------

SetLoadAddress:     sta DMACopyToDest + 0
                    stx DMACopyToDest + 1
                    sty Ory+1
                    lda DMACopyToDest + 2
                    and #$f0
Ory:                ora #$ef
                    sta DMACopyToDest + 2
MBank:              lda #$ef
                    sta DMACopyToDestination + 4
                    rts

// -----------------

LoadFile:           stx FileNamePtr
                    sty FileNamePtr+1

                    //First get directory listing track/sector
                    ldx #40
                    ldy #0
                    jsr ReadSector
                    bcs FileNotFoundError

                    //Read first directory
                    jsr CopyToBuffer
                    ldx FILE_BUFFER       //track, first byte of directory entry
                    ldy FILE_BUFFER + 1   //sector, second byte of directory entry

NextDirectoryPage:  jsr FetchNext
                    jsr CopyToBuffer

                    //Get first entry  pointer to next track/sector
                    ldy #$00
                    ldx #$00 //Store For beginning of entry
LoopEntry:          lda (BufferPtr), y
                    beq !+  //Dont store next track if 0
                    sta NextTrack
!:                  iny
                    lda (BufferPtr), y
                    beq !+ //Dont store next sector if 0
                    sta NextSector
!:                  iny

                    //FileType
                    lda (BufferPtr), y
                    iny

                    //Get this entries track/sector info
                    lda (BufferPtr), y
                    beq FileNotFoundError //Track 00 implies no file here
                    sta PotentialTrack
                    iny

                    lda (BufferPtr), y
                    sta PotentialSector
                    iny

                    ldz #$00
FilenameLoop:       lda (BufferPtr), y
                    cmp #$a0
                    beq FileFound
                    cmp (FileNamePtr), z
                    bne NextEntry
                    iny
                    inz
                    cpz #$10
                    bne FilenameLoop

FileFound:          txa
                    clc
                    adc #$1e
                    tay

                    //If a match set track/sector
                    lda PotentialTrack
                    sta NextTrack
                    lda PotentialSector
                    sta NextSector
                    jmp FetchFile

NextEntry:          //advance $20 bytes to next entry
                    txa
                    clc
                    adc #$20
                    tax
                    tay
                    bcc LoopEntry

                    //If crossing page is it still in the sector buffer?
                    jsr AdvanceSectorPtr //Returns 0 if we need to fetch next sector buffer
                    bne LoopEntry

                    //Otherwise we need to fetch new sector buffer
                    ldx <NextTrack
                    ldy <NextSector
                    jmp NextDirectoryPage

FileNotFoundError:  //Fall through into Floppy error below

FloppyError:
FloppyExit:         lda #$00
                    sta $d080
                    rts

FetchFile:
LoopFetchNext:      ldx NextTrack
                    ldy NextSector
                    jsr FetchNext
                    jsr CopyToBuffer

LoopFileRead:       ldy #$00
                    lda (BufferPtr), y
                    sta NextTrack
                    tax
                    iny
                    lda (BufferPtr), y
                    sta NextSector
                    taz
                    dez
                    iny
                    lda #$fe
                    cpx #$00
                    bne !+
                    tza
!:                  sta DMACopyToDestLength
                    jsr CopyFileToPosition
                    lda NextTrack
                    beq done

                    //Increase dest
                    clc
                    lda DMACopyToDest + 0
                    adc #$fe
                    sta DMACopyToDest + 0
                    bcc !+
                    inc DMACopyToDest + 1
                    bne !+
                    inc DMACopyToDest + 2
!:                  jsr AdvanceSectorPtr
                    bne LoopFileRead

                    //Otherwise we need to fetch new sector buffer
                    jmp LoopFetchNext

done:               bra FloppyExit

CopyFileToPosition: lda #$02
                    clc
                    adc SectorHalf
                    sta DMACopyToDestSource + 1

                    // Execute DMA job
                    lda #$00
                    sta $d702
                    sta $d704
                    lda #>DMACopyToDestination
                    sta $d701
                    lda #<DMACopyToDestination
                    sta $d705
                    rts

//Returns 0 if we need to fetch next sector buffer
AdvanceSectorPtr:   inc BufferPtr + 1
                    lda SectorHalf
                    eor #$01
                    sta SectorHalf
                    rts

//reads next sector, sets carry if an error occurs
FetchNext:          jsr ReadSector
                    bcc !+

                    // abort if the sector read failed
                    pla
                    pla //break out of the parent method
!:                  rts

//x = track (1 to 80), y = sector (0 to 39)
ReadSector:         //motor and LED on
                    lda #$60
                    sta $d080

                    //Wait for motor spin up
                    lda #$20
                    sta $d801

                    //(tracks begin at 0 not 1)
                    dex
                    stx $d084

                    //Convert sector
                    tya
                    lsr //Carry indicates we need second half of sector
                    tay

                    //(sectors begin at 1, not 0)
                    iny
                    sty $d085
                    lda #$00
                    sta $d086

                    //Apply carry to select sector
                    adc #$00
                    sta SectorHalf

                    // Read sector
                    lda #$41
                    sta $d081

                    //WaitForBusy
!:                  lda $d082
                    bmi !-

                    //Check for read error
                    lda $d082
                    and #$18
                    beq !+

                    // abort if the sector read failed
                    sec
!:                  rts

CopyToBuffer:       jsr CopySector
                    ldx #<FILE_BUFFER
                    stx BufferPtr + 0
                    lda #>FILE_BUFFER
                    adc SectorHalf
                    sta BufferPtr + 1
                    rts

CopySector:         //Set pointer to buffer
                    //Select FDC buffer
                    lda #$80
                    trb $d689

                    // Execute DMA job
                    lda #$00
                    sta $d702
                    sta $d704
                    lda #>DMACopyBuffer
                    sta $d701
                    lda #<DMACopyBuffer
                    sta $d705
                    rts

                      //DMA Job to copy from buffer at $200-$3FF to destination
DMACopyToDestination: .byte $0A  // Request format is F018A
                      .byte $80,$00 // Source is $00
                      .byte $81,$00 // Destination is $00
                      .byte $00  // No more options
                      // F018A DMA list
                      .byte 0 // copy + last request in chain
DMACopyToDestLength:  .word $00fe // size of copy
DMACopyToDestSource:  .word FILE_BUFFER + 2 // starting at
                      .byte $00   // of bank
DMACopyToDest:        .word $0800 // destination addr
                      .byte $00   // of bank
                      // !word $0000 // modulo (unused)

                      //DMA Job to copy 512 bytes from sector buffer
                      //at $FFD6C00 to temp buffer at $200-$3ff
DMACopyBuffer:        .byte $0A  // Request format is F018A
                      .byte $80,$FF // Source MB is $FFxxxxx
                      .byte $81,$00 // Destination MB is $00xxxxx
                      .byte $00  // No more options
                      //F018A DMA list
                      .byte 0  // copy + last request in chain
                      .word $0200 // size of copy
                      .word $6C00 // starting at
                      .byte $0D   // of bank
                      .word FILE_BUFFER // destination addr
                      .byte $00   // of bank
                      // !word $0000 // modulo (unused)

}
