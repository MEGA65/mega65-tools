char* instruction_lut[] = 
{
  // 4502 personality
    "BRK","ORA","CLE","SEE","TSB","ORA","ASL","RMB","PHP","ORA","ASL","TSY","TSB","ORA","ASL","BBR",
    "BPL","ORA","ORA","BPL","TRB","ORA","ASL","RMB","CLC","ORA","INC","INZ","TRB","ORA","ASL","BBR",
    "JSR","AND","JSR","JSR","BIT","AND","ROL","RMB","PLP","AND","ROL","TYS","BIT","AND","ROL","BBR",
    "BMI","AND","AND","BMI","BIT","AND","ROL","RMB","SEC","AND","DEC","DEZ","BIT","AND","ROL","BBR",
    "RTI","EOR","NEG","ASR","ASR","EOR","LSR","RMB","PHA","EOR","LSR","TAZ","JMP","EOR","LSR","BBR",
    "BVC","EOR","EOR","BVC","ASR","EOR","LSR","RMB","CLI","EOR","PHY","TAB","MAP","EOR","LSR","BBR",
    "RTS","ADC","RTS","BSR","STZ","ADC","ROR","RMB","PLA","ADC","ROR","TZA","JMP","ADC","ROR","BBR",
    "BVS","ADC","ADC","BVS","STZ","ADC","ROR","RMB","SEI","ADC","PLY","TBA","JMP","ADC","ROR","BBR",
    "BRA","STA","STA","BRA","STY","STA","STX","SMB","DEY","BIT","TXA","STY","STY","STA","STX","BBS",
    "BCC","STA","STA","BCC","STY","STA","STX","SMB","TYA","STA","TXS","STX","STZ","STA","STZ","BBS",
    "LDY","LDA","LDX","LDZ","LDY","LDA","LDX","SMB","TAY","LDA","TAX","LDZ","LDY","LDA","LDX","BBS",
    "BCS","LDA","LDA","BCS","LDY","LDA","LDX","SMB","CLV","LDA","TSX","LDZ","LDY","LDA","LDX","BBS",
    "CPY","CMP","CPZ","DEW","CPY","CMP","DEC","SMB","INY","CMP","DEX","ASW","CPY","CMP","DEC","BBS",
    "BNE","CMP","CMP","BNE","CPZ","CMP","DEC","SMB","CLD","CMP","PHX","PHZ","CPZ","CMP","DEC","BBS",
    "CPX","SBC","LDA","INW","CPX","SBC","INC","SMB","INX","SBC","EOM","ROW","CPX","SBC","INC","BBS",
    "BEQ","SBC","SBC","BEQ","PHW","SBC","INC","SMB","SED","SBC","PLX","PLZ","PHW","SBC","INC","BBS",

    // 6502 personality
    // XXX Currently just a copy of 4502 personality
    "BRK","ORA","KIL","SLO","NOP","ORA","ASL","SLO","PHP","ORA","ASL","ANC","NOP","ORA","ASL","SLO",
    "BPL","ORA","KIL","SLO","NOP","ORA","ASL","SLO","CLC","ORA","NOP","SLO","NOP","ORA","ASL","SLO",
    "JSR","AND","KIL","RLA","BIT","AND","ROL","RLA","PLP","AND","ROL","ANC","BIT","AND","ROL","RLA",
    "BMI","AND","KIL","RLA","NOP","AND","ROL","RLA","SEC","AND","NOP","RLA","NOP","AND","ROL","RLA",
    "RTI","EOR","KIL","SRE","NOP","EOR","LSR","SRE","PHA","EOR","LSR","ALR","JMP","EOR","LSR","SRE",
    "BVC","EOR","KIL","SRE","NOP","EOR","LSR","SRE","CLI","EOR","NOP","SRE","NOP","EOR","LSR","SRE",
    "RTS","ADC","KIL","RRA","NOP","ADC","ROR","RRA","PLA","ADC","ROR","ARR","JMP","ADC","ROR","RRA",
    "BVS","ADC","KIL","RRA","NOP","ADC","ROR","RRA","SEI","ADC","NOP","RRA","NOP","ADC","ROR","RRA",
    "NOP","STA","NOP","SAX","STY","STA","STX","SAX","DEY","NOP","TXA","XAA","STY","STA","STX","SAX",
    "BCC","STA","KIL","AHX","STY","STA","STX","SAX","TYA","STA","TXS","TAS","SHY","STA","SHX","AHX",
    "LDY","LDA","LDX","LAX","LDY","LDA","LDX","LAX","TAY","LDA","TAX","LAX","LDY","LDA","LDX","LAX",
    "BCS","LDA","KIL","LAX","LDY","LDA","LDX","LAX","CLV","LDA","TSX","LAS","LDY","LDA","LDX","LAX",
    "CPY","CMP","NOP","DCP","CPY","CMP","DEC","DCP","INY","CMP","DEX","AXS","CPY","CMP","DEC","DCP",
    "BNE","CMP","KIL","DCP","NOP","CMP","DEC","DCP","CLD","CMP","NOP","DCP","NOP","CMP","DEC","DCP",
    "CPX","SBC","NOP","ISC","CPX","SBC","INC","ISC","INX","SBC","NOP","SBC","CPX","SBC","INC","ISC",
    "BEQ","SBC","KIL","ISC","NOP","SBC","INC","ISC","SED","SBC","NOP","ISC","NOP","SBC","INC","ISC"
    };


// constant : mode_list := (
#define M_impl 0
#define M_InnX 1
#define M_nn 1
#define M_immnn 1
#define M_A 0
#define M_nnnn 2
#define M_nnrr 2
#define M_rr 1
#define M_InnY 1
#define M_InnZ 1
#define M_rrrr 2
#define M_nnX 1
#define M_nnnnY 2
#define M_nnnnX 2
#define M_Innnn 2
#define M_InnnnX 2
#define M_InnSPY 1
#define M_nnY 1
#define M_immnnnn 2

int mode_lut[] = 
{
  // 4502 personality first
    M_impl,  M_InnX,  M_impl,  M_impl,  M_nn,    M_nn,    M_nn,    M_nn,    
    M_impl,  M_immnn, M_A,     M_impl,  M_nnnn,  M_nnnn,  M_nnnn,  M_nnrr,  
    M_rr,    M_InnY,  M_InnZ,  M_rrrr,  M_nn,    M_nnX,   M_nnX,   M_nn,    
    M_impl,  M_nnnnY, M_impl,  M_impl,  M_nnnn,  M_nnnnX, M_nnnnX, M_nnrr,  
    M_nnnn,  M_InnX,  M_Innnn, M_InnnnX,M_nn,    M_nn,    M_nn,    M_nn,    
    M_impl,  M_immnn, M_A,     M_impl,  M_nnnn,  M_nnnn,  M_nnnn,  M_nnrr,  
    M_rr,    M_InnY,  M_InnZ,  M_rrrr,  M_nnX,   M_nnX,   M_nnX,   M_nn,    
    M_impl,  M_nnnnY, M_impl,  M_impl,  M_nnnnX, M_nnnnX, M_nnnnX, M_nnrr,  
    M_impl,  M_InnX,  M_impl,  M_impl,  M_nn,    M_nn,    M_nn,    M_nn,    
    M_impl,  M_immnn, M_A,     M_impl,  M_nnnn,  M_nnnn,  M_nnnn,  M_nnrr,  
    M_rr,    M_InnY,  M_InnZ,  M_rrrr,  M_nnX,   M_nnX,   M_nnX,   M_nn,    
    M_impl,  M_nnnnY, M_impl,  M_impl,  M_impl,  M_nnnnX, M_nnnnX, M_nnrr,
    // $63 BSR $nnnn is 16-bit relative on the 4502.  We treat it as absolute
    // mode, with microcode being used to select relative addressing.
    M_impl,  M_InnX,  M_immnn, M_nnnn,  M_nn,    M_nn,    M_nn,    M_nn,    
    M_impl,  M_immnn, M_A,     M_impl,  M_Innnn, M_nnnn,  M_nnnn,  M_nnrr,  
    M_rr,    M_InnY,  M_InnZ,  M_rrrr,  M_nnX,   M_nnX,   M_nnX,   M_nn,    
    M_impl,  M_nnnnY, M_impl,  M_impl,  M_InnnnX,M_nnnnX, M_nnnnX, M_nnrr,  
    M_rr,    M_InnX,  M_InnSPY,M_rrrr,  M_nn,    M_nn,    M_nn,    M_nn,    
    M_impl,  M_immnn, M_impl,  M_nnnnX, M_nnnn,  M_nnnn,  M_nnnn,  M_nnrr,  
    M_rr,    M_InnY,  M_InnZ,  M_rrrr,  M_nnX,   M_nnX,   M_nnY,   M_nn,    
    M_impl,  M_nnnnY, M_impl,  M_nnnnY, M_nnnn,  M_nnnnX, M_nnnnX, M_nnrr,  
    M_immnn, M_InnX,  M_immnn, M_immnn, M_nn,    M_nn,    M_nn,    M_nn,    
    M_impl,  M_immnn, M_impl,  M_nnnn,  M_nnnn,  M_nnnn,  M_nnnn,  M_nnrr,  
    M_rr,    M_InnY,  M_InnZ,  M_rrrr,  M_nnX,   M_nnX,   M_nnY,   M_nn,
    M_impl,  M_nnnnY, M_impl,  M_nnnnX, M_nnnnX, M_nnnnX, M_nnnnY, M_nnrr,  
    M_immnn, M_InnX,  M_immnn, M_nn,    M_nn,    M_nn,    M_nn,    M_nn,    
    M_impl,  M_immnn, M_impl,  M_nnnn,  M_nnnn,  M_nnnn,  M_nnnn,  M_nnrr,  
    M_rr,    M_InnY,  M_InnZ,  M_rrrr,  M_nn,    M_nnX,   M_nnX,   M_nn,
    M_impl,  M_nnnnY, M_impl,  M_impl,  M_nnnn,  M_nnnnX, M_nnnnX, M_nnrr,  
    M_immnn, M_InnX,  M_InnSPY,M_nn,    M_nn,    M_nn,    M_nn,    M_nn,    
    M_impl,  M_immnn, M_impl,  M_nnnn,  M_nnnn,  M_nnnn,  M_nnnn,  M_nnrr,  
    M_rr,    M_InnY,  M_InnZ,  M_rrrr,  M_immnnnn,M_nnX,   M_nnX,   M_nn,    
    M_impl,  M_nnnnY, M_impl,  M_impl,  M_nnnn,  M_nnnnX, M_nnnnX, M_nnrr,

    // 6502 personality
    // XXX currently just a copy of 4502 personality
    M_impl,M_InnX,M_impl,M_InnX,M_nn,M_nn,M_nn,M_nn,
    M_impl,M_immnn,M_impl,M_immnn,M_nnnn,M_nnnn,M_nnnn,M_nnnn,
    M_rr,M_InnY,M_impl,M_InnY,M_nnX,M_nnX,M_nnX,M_nnX,
    M_impl,M_nnnnY,M_impl,M_nnnnY,M_nnnnX,M_nnnnX,M_nnnnX,M_nnnnX,
    M_nnnn,M_InnX,M_impl,M_InnX,M_nn,M_nn,M_nn,M_nn,
    M_impl,M_immnn,M_impl,M_immnn,M_nnnn,M_nnnn,M_nnnn,M_nnnn,
    M_rr,M_InnY,M_impl,M_InnY,M_nnX,M_nnX,M_nnX,M_nnX,
    M_impl,M_nnnnY,M_impl,M_nnnnY,M_nnnnX,M_nnnnX,M_nnnnX,M_nnnnX,
    M_impl,M_InnX,M_impl,M_InnX,M_nn,M_nn,M_nn,M_nn,
    M_impl,M_immnn,M_impl,M_immnn,M_nnnn,M_nnnn,M_nnnn,M_nnnn,
    M_rr,M_InnY,M_impl,M_InnY,M_nnX,M_nnX,M_nnX,M_nnX,
    M_impl,M_nnnnY,M_impl,M_nnnnY,M_nnnnX,M_nnnnX,M_nnnnX,M_nnnnX,
    M_impl,M_InnX,M_impl,M_InnX,M_nn,M_nn,M_nn,M_nn,
    M_impl,M_immnn,M_impl,M_immnn,M_Innnn,M_nnnn,M_nnnn,M_nnnn,
    M_rr,M_InnY,M_impl,M_InnY,M_nnX,M_nnX,M_nnX,M_nnX,
    M_impl,M_nnnnY,M_impl,M_nnnnY,M_nnnnX,M_nnnnX,M_nnnnX,M_nnnnX,
    M_immnn,M_InnX,M_immnn,M_InnX,M_nn,M_nn,M_nn,M_nn,
    M_impl,M_immnn,M_impl,M_immnn,M_nnnn,M_nnnn,M_nnnn,M_nnnn,
    M_rr,M_InnY,M_impl,M_InnY,M_nnX,M_nnX,M_nnY,M_nnY,
    M_impl,M_nnnnY,M_impl,M_nnnnY,M_nnnnX,M_nnnnX,M_nnnnY,M_nnnnY,
    M_immnn,M_InnX,M_immnn,M_InnX,M_nn,M_nn,M_nn,M_nn,
    M_impl,M_immnn,M_impl,M_immnn,M_nnnn,M_nnnn,M_nnnn,M_nnnn,
    M_rr,M_InnY,M_impl,M_InnY,M_nnX,M_nnX,M_nnY,M_nnY,
    M_impl,M_nnnnY,M_impl,M_nnnnY,M_nnnnX,M_nnnnX,M_nnnnY,M_nnnnY,
    M_immnn,M_InnX,M_immnn,M_InnX,M_nn,M_nn,M_nn,M_nn,
    M_impl,M_immnn,M_impl,M_immnn,M_nnnn,M_nnnn,M_nnnn,M_nnnn,
    M_rr,M_InnY,M_impl,M_InnY,M_nnX,M_nnX,M_nnX,M_nnX,
    M_impl,M_nnnnY,M_impl,M_nnnnY,M_nnnnX,M_nnnnX,M_nnnnX,M_nnnnX,
    M_immnn,M_InnX,M_immnn,M_InnX,M_nn,M_nn,M_nn,M_nn,
    M_impl,M_immnn,M_impl,M_immnn,M_nnnn,M_nnnn,M_nnnn,M_nnnn,
    M_rr,M_InnY,M_impl,M_InnY,M_nnX,M_nnX,M_nnX,M_nnX,
    M_impl,M_nnnnY,M_impl,M_nnnnY,M_nnnnX,M_nnnnX,M_nnnnX,M_nnnnX
};
