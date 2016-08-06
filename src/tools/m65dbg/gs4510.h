extern char* instruction_lut[];

typedef struct
{
  char* name;
  int   val;
} type_opcode_mode;

typedef enum {
  M_impl,
  M_InnX,
  M_nn,
  M_immnn,
  M_A,
  M_nnnn,
  M_nnrr,
  M_rr,
  M_InnY,
  M_InnZ,
  M_rrrr,
  M_nnX,
  M_nnnnY,
  M_nnnnX,
  M_Innnn,
  M_InnnnX,
  M_InnSPY,
  M_nnY,
  M_immnnnn
} mode_list;

extern mode_list mode_lut[];
extern type_opcode_mode opcode_mode[];
