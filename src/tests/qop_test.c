/*
  Comprehensive Q-Opcode Test Suite

  To check if bitstream behaves as expected and to aid xemu implementation
*/

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <memory.h>
#include <tests.h>

typedef struct {
  // read result value from where?
  //  0 == opcode save area (0x038c)
  //  1 == val2 area (0x384)
  //  2 == base page address (set in suite)
  unsigned char rmw;
  // the opcode!
  unsigned char opcode;
  // which addressing mode / flags:
  //  0x01 - base page
  //  0x02 - absolute
  //  0x04 - base page indirect z indexed
  //  0x08 - 32-bit base page indirect z indexed
  //  0x10 - base bage x indexed
  //  0x20 - absolute x indexed
  //  0x40 - accumulator/Q
  //  0x80 - SEC before executing (without: CLC)
  unsigned char mode;
  // some name
  unsigned char name[16]; // 15 max!
} opcode_mode;

typedef struct {
  // modes to which this test is applied to (mask)
  unsigned char apply_mode;
  // added to mode name
  unsigned char name[10]; // 9 max!
  // our three test values
  unsigned long val1;
  unsigned long val2;
  unsigned long val3;
  // the result that we expect
  unsigned long expected;
  // mask and value for the status register
  unsigned char flag_mask;
  unsigned char flag_val;
  // offset for reading the result back (X indexed)
  unsigned char read_offset;
} opcode_test;

typedef struct {
  // name of the test suite
  unsigned char name[16];
  // offsets into code and basepage address to use
  unsigned char offset1, offset2, basepage;
  // the modes we are testing (only one can be variable)
  opcode_mode modes[6];
  // the test cases we are trying
  opcode_test tests[];
} opcode_suite;

/*
4242A5   LDQ $nn
4242AD   LDQ $nnnn
4242B2   LDQ ($nn),Z
4242EAB2 LDQ [$nn],Z
*/

opcode_suite test_ldq =
  {
    "ldq", 63, 16, 0x64,
    {
      {0, 0xa5, 0x01, "ldq $nn"},
      {0, 0xad, 0x02, "ldq $nnnn"},
      {0, 0xb2, 0x04, "ldq ($nn),z"},
      {0, 0xb2, 0x08, "ldq [$nn],z"},
      {0, 0}
    },
    {
      // LDQ - check if right value is loaded, and is Q ops are working
      // base page adressing, val1 needs to set z=0 for bp indirect z indexed!
      {0xf, " -nz",  0x00345678, 0x12345678, 0x12345678, 0x12345678, 0x82, 0x00, 0},
      {0xf, " +z-n", 0x00345678, 0x00000000, 0x12345678, 0x00000000, 0x82, 0x02, 0},
      {0xf, " +n-z", 0x00345678, 0xfedcba98, 0x12345678, 0xfedcba98, 0x82, 0x80, 0},
      // tests only for bp indirect z indexed, val1 sets z=2
      {0xc, "=2 -nz",  0x02345678, 0x56781234, 0xfedc1234, 0x12345678, 0x82, 0x00, 0},
      {0xc, "=2 +z-n", 0x02345678, 0x0000ffff, 0xfedc0000, 0x00000000, 0x82, 0x02, 0},
      {0xc, "=2 +n-z", 0x02345678, 0xfedcba98, 0xfedcba98, 0xba98fedc, 0x82, 0x80, 0},
      {0}
    }
  };

/*
424285   STQ $nn
42428D   STQ $nnnn
424292   STQ ($nn)
4242EA92 STQ [$nn]
*/

opcode_suite test_stq =
  {
    "stq", 63, 16, 0x64,
    {
      {2, 0x85, 0x01, "stq $nn"},
      {1, 0x8d, 0x02, "stq $nnnn"},
      {1, 0x92, 0x04, "stq ($nn)"},
      {1, 0x92, 0x08, "stq [$nn]"},
      {0, 0}
    },
    {
      {0xf, "", 0x02345678, 0xfedcba98, 0x12345678, 0x02345678, 0x00, 0x00, 0},
      {0},
    }
  };

/*
424265   ADCQ $nn
42426D   ADCQ $nnnn
424272   ADCQ ($nn)
4242EA72 ADCQ [$nn]
*/

opcode_suite test_adcq =
  {
    "adcq", 63, 16, 0x64,
    {
      {0, 0x65, 0x01, "adcq $nn"},
      {0, 0x6d, 0x02, "adcq $nnnn"},
      {0, 0x72, 0x04, "adcq ($nn)"},
      {0, 0x72, 0x08, "adcq [$nn]"},
      {0, 0},
    },
    {
      // base page adressing
      // 12345678 + 10fedcba = 22345678 -- NVZC all unset
      {0xf, " -nvzc",  0x12345678, 0x10fedcba, 0x12345678, 0x23333332, 0xc3, 0x00, 0},
      // 12345678 + ffffffff = 12345677 -- C set, NVZ unset
      {0xf, " +c-nvz", 0x12345678, 0xffffffff, 0x12345678, 0x12345677, 0xc3, 0x01, 0},
      // 7f123456 + 10fedcba = 90111110 -- NV set, ZC unset
      {0xf, " +nv-zc", 0x7f123456, 0x10fedcba, 0x12345678, 0x90111110, 0xc3, 0xc0, 0},
      // 81234567 + 8fedcba9 = 11111110 -- VC set, NZ unset
      {0xf, " +vc-nz", 0x81234567, 0x8fedcba9, 0x12345678, 0x11111110, 0xc3, 0x41, 0},
      // 12345678 + 81fedcba = a2222219 -- N set, VZC unset
      {0xf, " +n-vzc", 0x12345678, 0x8fedcba1, 0x12345678, 0xa2222219, 0xc3, 0x80, 0},
      // 12345678 + edcba988 = 00000000 -- ZC set, NV unset
      {0xf, " +zc-nv", 0x12345678, 0xedcba988, 0x12345678, 0x00000000, 0xc3, 0x03, 0},
      // 81234567 + 81234567 = 02468ace -- VC set, NZ unset
      {0xf, " +n-vzc", 0x81234567, 0x81234567, 0x12345678, 0x02468ace, 0xc3, 0x41, 0},
      {0}
    }
  };

/*
4242E5   SBCQ $nn
4242ED   SBCQ $nnnn
4242F2   SBCQ ($nn)
4242EAF2 SBCQ [$nn]
*/

opcode_suite test_sbcq =
  {
    "sbcq", 63, 16, 0x64,
    {
      {0, 0xe5, 0x81, "sbcq $nn"},
      {0, 0xed, 0x82, "sbcq $nnnn"},
      {0, 0xf2, 0x84, "sbcq ($nn)"},
      {0, 0xf2, 0x88, "sbcq [$nn]"},
      {0, 0},
    },
    {
      // 12345678 - 10fedcba = 22345678 -- C set, NVZ unset
      {0xf, " +c-nvz", 0x12345678, 0x10fedcba, 0xaaddeeff, 0x013579be, 0xc3, 0x01, 0},
      // 12345678 - ffffffff = 12345677 -- NVZC all unset
      {0xf, " -nvzc",  0x12345678, 0xffffffff, 0xaaddeeff, 0x12345679, 0xc3, 0x00, 0},
      // 10fedcba - 7f123456 = 91eca864 -- N set, VZC unset
      {0xf, " +n-vzc", 0x10fedcba, 0x7f123456, 0xaaddeeff, 0x91eca864, 0xc3, 0x80, 0},
      // 10fedcba - 10fedcba = 00000000 -- ZC set, NV unset
      {0xf, " +zc-nv", 0x10fedcba, 0x10fedcba, 0xaaddeeff, 0x00000000, 0xc3, 0x03, 0},
      // 80000000 - 12345678 = 6dcba988 -- VC set, NZ unset
      {0xf, " +vc-nz", 0x80000000, 0x12345678, 0xaaddeeff, 0x6dcba988, 0xc3, 0x41, 0},
      // 7fedcba9 - fedcba98 = 81111111 -- NV set, ZC unset
      {0xf, " +nv-zc", 0x7fedcba9, 0xfedcba98, 0xaaddeeff, 0x81111111, 0xc3, 0xc0, 0},
      {0}
    }
  };

/*
4242C5   CMPQ $nn
4242CD   CMPQ $nnnn
4242D2   CMPQ ($nn)
4242EAD2 CMPQ [$nn]
*/

opcode_suite test_cmpq =
  {
    "cmpq", 63, 16, 0x64,
    {
      {0, 0xc5, 0x01, "cmpq $nn"},
      {0, 0xcd, 0x02, "cmpq $nnnn"},
      {0, 0xd2, 0x04, "cmpq ($nn)"},
      {0, 0xd2, 0x08, "cmpq [$nn]"},
      {0, 0},
    },
    {
      // 12345678 cmp 12345678 (equal)
      {0xf, " + a=m", 0x12345678, 0x12345678, 0xaaddeeff, 0x12345678, 0xc3, 0x03, 0},
      // 12345678 cmp 01234567 (a > b)
      {0xf, " + a>m", 0x12345678, 0x01234567, 0xaaddeeff, 0x12345678, 0xc3, 0x01, 0},
      // 12345678 cmp 23456789 (a < b)
      {0xf, " + a<m", 0x12345678, 0x23456789, 0xaaddeeff, 0x12345678, 0xc3, 0x80, 0},
      // fedcba98 cmp fedcba98 (equal)
      {0xf, " - a=m", 0xfedcba98, 0xfedcba98, 0xaaddeeff, 0xfedcba98, 0xc3, 0x03, 0},
      // fedcba98 cmp edcba987 (a > b)
      {0xf, " - a>m", 0xfedcba98, 0xedcba987, 0xaaddeeff, 0xfedcba98, 0xc3, 0x01, 0},
      // edcba987 cmp fedcba98 (a < b)
      {0xf, " - a<m", 0xedcba987, 0xfedcba98, 0xaaddeeff, 0xedcba987, 0xc3, 0x80, 0},
      // 12345678 cmp edcba987 (a < b)
      {0xf, " +- a<m", 0x12345678, 0xedcba987, 0xaaddeeff, 0x12345678, 0xc3, 0x00, 0},
      // edcba987 cmp 12345678 (a > b)
      {0xf, " -+ a>m", 0xedcba987, 0x12345678, 0xaaddeeff, 0xedcba987, 0xc3, 0x81, 0},
      {0}
    }
  };

/*
424224   BITQ $nn
42422C   BITQ $nnnn
*/

opcode_suite test_bitq =
  {
    "bitq", 63, 16, 0x64,
    {
      {0, 0x24, 0x01, "bitq $nn"},
      {0, 0x2c, 0x02, "bitq $nnnn"},
      {0, 0},
    },
    {
      {0xf, " -nvz",  0x12345678, 0x02345678, 0xaaddeeff, 0x12345678, 0xc2, 0x00, 0},
      {0xf, " +v-nz", 0x42ff56ff, 0x42345678, 0xaaddeeff, 0x42ff56ff, 0xc2, 0x40, 0},
      {0xf, " +n-vz", 0x82ff56ff, 0x82345678, 0xaaddeeff, 0x82ff56ff, 0xc2, 0x80, 0},
      {0xf, " +nv-z", 0xc2ff56ff, 0xc2345678, 0xaaddeeff, 0xc2ff56ff, 0xc2, 0xc0, 0},
      {0xf, " +z-nv", 0x02345678, 0x00000000, 0xaaddeeff, 0x02345678, 0xc2, 0x02, 0},
      {0xf, " +zv-n", 0x02345678, 0x40000000, 0xaaddeeff, 0x02345678, 0xc2, 0x42, 0},
      {0xf, " +zn-v", 0x02345678, 0x80000000, 0xaaddeeff, 0x02345678, 0xc2, 0x82, 0},
      {0xf, " +znv",  0x02345678, 0xc0000000, 0xaaddeeff, 0x02345678, 0xc2, 0xc2, 0},
      {0}
    }
  };

/*
42423A   DEQ
4242C6   DEQ $nn
4242CE   DEQ $nnnn
4242D6   DEQ $nn,X
4242DE   DEQ $nnnn,X
*/

opcode_suite test_deq =
  {
    "deq", 63, 16, 0x64,
    {
      {0, 0x3a, 0x40, "deq q"},
      {2, 0xc6, 0x01, "deq $nn"},
      {1, 0xce, 0x02, "deq $nnnn"},
      {2, 0xd6, 0x10, "deq $nn,x"},
      {1, 0xde, 0x20, "deq $nnnn,x"},
      {0, 0},
    },
    {
      // only for implied Q-accumulator
      {0x40, " -nz",  0x12345600, 0x12345678, 0xaaddeeff, 0x123455ff, 0x82, 0x00, 0},
      //{0x40, " -nz",  0x00000100, 0x12345678, 0xaaddeeff, 0x000000ff, 0x82, 0x00, 0},  // extra test, not really neeeded
      {0x40, " +z-n", 0x00000001, 0x12345678, 0xaaddeeff, 0x00000000, 0x82, 0x02, 0},  // will succeed, because dec a will satisfy
      {0x40, " +n-z", 0x00000000, 0x12345678, 0xaaddeeff, 0xffffffff, 0x82, 0x80, 0},
      // x=0 for these tests
      {0x33, " -nz",  0x12340078, 0x12345600, 0xaaddeeff, 0x123455ff, 0x82, 0x00, 0},
      {0x33, " +z-n", 0x12340078, 0x00000001, 0xaaddeeff, 0x00000000, 0x82, 0x02, 0},
      {0x33, " +n-z", 0x12340078, 0x00000000, 0xaaddeeff, 0xffffffff, 0x82, 0x80, 0},
      // x=2 for the next, also need to set read_offset <= val1(X/15..8)
      {0x30, " -nz",  0x12340278, 0x5600aadd, 0xeeff1234, 0x123455ff, 0x82, 0x00, 2},
      {0x30, " +z-n", 0x12340278, 0x0001aadd, 0xeeff0000, 0x00000000, 0x82, 0x02, 2},
      {0x30, " +n-z", 0x12340278, 0x0000aadd, 0xeeff0000, 0xffffffff, 0x82, 0x80, 2},
      {0}
    }
  };

/*
42421A   INQ
4242E6   INQ $nn
4242EE   INQ $nnnn
4242F6   INQ $nn,X
4242FE   INQ $nnnn,X
*/

opcode_suite test_inq =
  {
    "inq", 63, 16, 0x64,
    {
      {0, 0x1a, 0x40, "inq q"},
      {2, 0xe6, 0x01, "inq $nn"},
      {1, 0xee, 0x02, "inq $nnnn"},
      {2, 0xf6, 0x10, "inq $nn,x"},
      {1, 0xfe, 0x20, "inq $nnnn,x"},
      {0, 0},
    },
    {
      // only for implied Q-accumulator
      {0x40, " -nz",  0x123456ff, 0x12345678, 0xaaddeeff, 0x12345700, 0x82, 0x00, 0},
      {0x40, " +z-n", 0xffffffff, 0x12345678, 0xaaddeeff, 0x00000000, 0x82, 0x02, 0},
      {0x40, " +n-z", 0x7fffffff, 0x12345678, 0xaaddeeff, 0x80000000, 0x82, 0x80, 0},
      // x=0 for these tests
      {0x33, " -nz",  0x12340078, 0x123456ff, 0xaaddeeff, 0x12345700, 0x82, 0x00, 0},
      {0x33, " +z-n", 0x12340078, 0xffffffff, 0xaaddeeff, 0x00000000, 0x82, 0x02, 0},
      {0x33, " +n-z", 0x12340078, 0x7fffffff, 0xaaddeeff, 0x80000000, 0x82, 0x80, 0},
      // x=2 for the next, also need to set read_offset <= val1(X/15..8)
      {0x30, " -nz",  0x12340278, 0x56ffaadd, 0xeeff1234, 0x12345700, 0x82, 0x00, 2},
      {0x30, " +z-n", 0x12340278, 0xffffaadd, 0xeeffffff, 0x00000000, 0x82, 0x02, 2},
      {0x30, " +n-z", 0x12340278, 0xffffaadd, 0xeeff7fff, 0x80000000, 0x82, 0x80, 2},
      {0}
    }
  };

/*
424225   ANDQ $nn
42422D   ANDQ $nnnn
424232   ANDQ ($nn)
4242EA32 ANDQ [$nn]
*/

opcode_suite test_andq =
  {
    "andq", 63, 16, 0x64,
    {
      {0, 0x25, 0x01, "andq $nn"},
      {0, 0x2d, 0x02, "andq $nnnn"},
      {0, 0x32, 0x04, "andq ($nn)"},
      {0, 0x32, 0x08, "andq [$nn]"},
      {0, 0},
    },
    {
      {0xf, " -nz",  0x02345678, 0x12345678, 0x00000000, 0x02345678, 0x82, 0x00, 0},
      {0xf, " +z-n", 0x025555aa, 0x0daaaa55, 0xffffffff, 0x00000000, 0x82, 0x02, 0},
      {0xf, " +n-z", 0xfe4d52e6, 0xcb38b4d9, 0x00000000, 0xca0810c0, 0x82, 0x80, 0},
      {0}
    }
  };

/*
424205   ORQ $nn
42420D   ORQ $nnnn
424212   ORQ ($nn)
4242EA12 ORQ [$nn]
*/

opcode_suite test_orq =
  {
    "orq", 63, 16, 0x64,
    {
      {0, 0x05, 0x01, "orq $nn"},
      {0, 0x0d, 0x02, "orq $nnnn"},
      {0, 0x12, 0x04, "orq ($nn)"},
      {0, 0x12, 0x08, "orq [$nn]"},
      {0, 0},
    },
    {
      {0xf, " -nz",  0x02345678, 0x514b17cd, 0x00000000, 0x537f57fd, 0x82, 0x00, 0},
      {0xf, " +z-n", 0x00000000, 0x00000000, 0xffffffff, 0x00000000, 0x82, 0x02, 0}, // stupid test
      {0xf, " +n-z", 0x02345678, 0xabcdef12, 0xffff0000, 0xabfdff7a, 0x82, 0x80, 0},
      {0}
    }
  };

/*
424245   EORQ $nn
42424D   EORQ $nnnn
424252   EORQ ($nn)
4242EA52 EORQ [$nn]
*/

opcode_suite test_eorq =
  {
    "eorq", 63, 16, 0x64,
    {
      {0, 0x45, 0x01, "eorq $nn"},
      {0, 0x4d, 0x02, "eorq $nnnn"},
      {0, 0x52, 0x04, "eorq ($nn)"},
      {0, 0x52, 0x08, "eorq [$nn]"},
      {0, 0},
    },
    {
      {0xf, " -nz",  0x02345678, 0x524b17cd, 0x00000000, 0x507f41b5, 0x82, 0x00, 0},
      {0xf, " +z-n", 0x55aaaa55, 0x55aaaa55, 0x00000000, 0x00000000, 0x82, 0x02, 0},
      {0xf, " +n-z", 0x02345678, 0xfda577c9, 0x00000000, 0xff9121b1, 0x82, 0x80, 0},
      {0}
    }
  };

/*
42424A   LSRQ
424246   LSRQ $nn
42424E   LSRQ $nnnn
424256   LSRQ $nn,X
42425E   LSRQ $nnnn,X
*/

opcode_suite test_lsrq =
  {
    "lsrq", 63, 16, 0x64,
    {
      {0, 0x4A, 0x40, "lsrq q"},
      {2, 0x46, 0x01, "lsrq $nn"},
      {1, 0x4e, 0x02, "lsrq $nnnn"},
      {2, 0x56, 0x10, "lsrq $nn,x"},
      {1, 0x5e, 0x20, "lsrq $nnnn,x"},
      {0, 0},
    },
    {
      // implied Q
      {0x40, " -nzc",  0x02345678, 0xe2345678, 0xffffffff, 0x011a2b3c, 0x83, 0x00, 0},
      {0x40, " +zc-n", 0x00000001, 0xd2345678, 0xffffffff, 0x00000000, 0x83, 0x03, 0},
      {0x40, " +c-nz", 0x03456789, 0xf2345678, 0xffffffff, 0x01a2b3c4, 0x83, 0x01, 0},
      // test with x=0
      {0x33, " -nzc",  0x02340078, 0x02345678, 0xffffffff, 0x011a2b3c, 0x83, 0x00, 0},
      {0x33, " +zc-n", 0x02340078, 0x00000001, 0xffffffff, 0x00000000, 0x83, 0x03, 0},
      {0x33, " +c-nz", 0x02340078, 0x03456789, 0xffffffff, 0x01a2b3c4, 0x83, 0x01, 0},
      // it is not possible to logical shift right and get a negative result!
      // test with x=2, offset 2
      {0x30, " -nzc",  0x02340278, 0x5678ffff, 0xffff0234, 0x011a2b3c, 0x83, 0x00, 2},
      {0x30, " +zc-n", 0x02340278, 0x0001ffff, 0xffff0000, 0x00000000, 0x83, 0x03, 2},
      {0x30, " +c-nz", 0x02340278, 0x5679ffff, 0xffff0234, 0x011a2b3c, 0x83, 0x01, 2},
      {0}
    }
  };

/*
424243   ASRQ
424244   ASRQ $nn
424254   ASRQ $nn,X
*/

opcode_suite test_asrq =
  {
    "asrq", 63, 16, 0x64,
    {
      {0, 0x43, 0x40, "asrq q"},
      {2, 0x44, 0x01, "asrq $nn"},
      {2, 0x54, 0x10, "asrq $nn,x"},
      {0, 0},
    },
    {
      // implied Q
      {0x40, " -nzc",  0x02345678, 0xb2340078, 0xffffffff, 0x011a2b3c, 0x83, 0x00, 0},
      {0x40, " +zc-n", 0x00000001, 0xc2340078, 0xffffffff, 0x00000000, 0x83, 0x03, 0},
      {0x40, " +c-nz", 0x03456789, 0xd2340078, 0xffffffff, 0x01a2b3c4, 0x83, 0x01, 0},
      {0x40, " +n-zc", 0xf0123456, 0xe2340078, 0xffffffff, 0xf8091a2b, 0x83, 0x80, 0},
      {0x40, " +nc-z", 0xf0234567, 0xf2340078, 0xffffffff, 0xf811a2b3, 0x83, 0x81, 0},
      // test with x=0
      {0x11, " -nzc",  0x02340078, 0x02345678, 0xffffffff, 0x011a2b3c, 0x83, 0x00, 0},
      {0x11, " +zc-n", 0x02340078, 0x00000001, 0xffffffff, 0x00000000, 0x83, 0x03, 0},
      {0x11, " +c-nz", 0x02340078, 0x03456789, 0xffffffff, 0x01a2b3c4, 0x83, 0x01, 0},
      {0x11, " +n-zc", 0x02340078, 0xf0123456, 0xffffffff, 0xf8091a2b, 0x83, 0x80, 0},
      {0x11, " +nc-z", 0x02340078, 0xf0234567, 0xffffffff, 0xf811a2b3, 0x83, 0x81, 0},
      // test with x=2, offset 2
      {0x10, " -nzc",  0x02340278, 0x5678ffff, 0xffff0234, 0x011a2b3c, 0x83, 0x00, 2},
      {0x10, " +zc-n", 0x02340278, 0x0001ffff, 0xffff0000, 0x00000000, 0x83, 0x03, 2},
      {0x10, " +c-nz", 0x02340278, 0x5679ffff, 0xffff0234, 0x011a2b3c, 0x83, 0x01, 2},
      {0x10, " +n-zc", 0x02340278, 0x3456dddd, 0xeeeef012, 0xf8091a2b, 0x83, 0x80, 2},
      {0x10, " +nc-z", 0x02340278, 0x4567eeee, 0xddddf023, 0xf811a2b3, 0x83, 0x81, 2},
      {0}
    }
  };

/*
42420A   ASLQ
424206   ASLQ $nn
42420E   ASLQ $nnnn
424216   ASLQ $nn,X
42421E   ASLQ $nnnn,X
*/

opcode_suite test_aslq =
  {
    "aslq", 63, 16, 0x64,
    {
      // set carry with all the tests, to check it is not shifted in
      {0, 0x0A, 0xc0, "aslq q"},
      {2, 0x06, 0x81, "aslq $nn"},
      {1, 0x0e, 0x82, "aslq $nnnn"},
      {2, 0x16, 0x90, "aslq $nn,x"},
      {1, 0x1e, 0xa0, "aslq $nnnn,x"},
      {0, 0},
    },
    {
      // implied Q
      {0x40, " -nzc",  0x02c45678, 0xe2340078, 0x00000000, 0x0588acf0, 0x83, 0x00, 0},
      {0x40, " +zc-n", 0x80000000, 0xd2340078, 0x00000000, 0x00000000, 0x83, 0x03, 0},
      {0x40, " +c-nz", 0x82345679, 0xc2340078, 0x00000000, 0x0468acf2, 0x83, 0x01, 0},
      {0x40, " +n-zc", 0x7f345679, 0xb2340078, 0x00000000, 0xfe68acf2, 0x83, 0x80, 0},
      {0x40, " +nc-z", 0xfedcba98, 0xa2340078, 0x00000000, 0xfdb97530, 0x83, 0x81, 0},
      // tests with x=0
      {0x33, " -nzc",  0x02340078, 0x02c45678, 0x00000000, 0x0588acf0, 0x83, 0x00, 0},
      {0x33, " +zc-n", 0x02340078, 0x80000000, 0x00000000, 0x00000000, 0x83, 0x03, 0},
      {0x33, " +c-nz", 0x02340078, 0x82345679, 0x00000000, 0x0468acf2, 0x83, 0x01, 0},
      {0x33, " +n-zc", 0x02340078, 0x7f345679, 0x00000000, 0xfe68acf2, 0x83, 0x80, 0},
      {0x33, " +nc-z", 0x02340078, 0xfedcba98, 0x00000000, 0xfdb97530, 0x83, 0x81, 0},
      // test with x=2, offset 2
      {0x30, " -nzc",  0x02340278, 0x56780000, 0x000002c4, 0x0588acf0, 0x83, 0x00, 2},
      {0x30, " +zc-n", 0x02340278, 0x0000ffff, 0xffff8000, 0x00000000, 0x83, 0x03, 2},
      {0x30, " +c-nz", 0x02340278, 0x5679dddd, 0xeeee8234, 0x0468acf2, 0x83, 0x01, 2},
      {0x30, " +n-zc", 0x02340278, 0x5679abcd, 0x34567f34, 0xfe68acf2, 0x83, 0x80, 2},
      {0x30, " +nc-z", 0x02340278, 0xba984567, 0xabcdfedc, 0xfdb97530, 0x83, 0x81, 2},
      {0}
    }
  };

/*
42426A   RORQ
424266   RORQ $nn
42426E   RORQ $nnnn
424276   RORQ $nn,X
42427E   RORQ $nnnn,X
*/

opcode_suite test_rorq0 =
  {
    "rorq c=0", 63, 16, 0x64,
    {
      // test with carry clear -> no negative result possible
      {0, 0x6a, 0x40, "rorq0 q"},
      {2, 0x66, 0x01, "rorq0 $nn"},
      {1, 0x6e, 0x02, "rorq0 $nnnn"},
      {2, 0x76, 0x10, "rorq0 $nn,x"},
      {1, 0x7e, 0x20, "rorq0 $nnnn,x"},
      {0, 0},
    },
    {
      // implied Q
      {0x40, " -nzc",  0x02345678, 0xb2340078, 0xffffffff, 0x011a2b3c, 0x83, 0x00, 0},
      {0x40, " +zc-n", 0x00000001, 0xc2340078, 0xffffffff, 0x00000000, 0x83, 0x03, 0},
      {0x40, " +c-nz", 0x03456789, 0xd2340078, 0xffffffff, 0x01a2b3c4, 0x83, 0x01, 0},
      // test with x=0
      {0x11, " -nzc",  0x02340078, 0x02345678, 0xffffffff, 0x011a2b3c, 0x83, 0x00, 0},
      {0x11, " +zc-n", 0x02340078, 0x00000001, 0xffffffff, 0x00000000, 0x83, 0x03, 0},
      {0x11, " +c-nz", 0x02340078, 0x03456789, 0xffffffff, 0x01a2b3c4, 0x83, 0x01, 0},
      // test with x=2, offset 2
      {0x10, " -nzc",  0x02340278, 0x5678ffff, 0xffff0234, 0x011a2b3c, 0x83, 0x00, 2},
      {0x10, " +zc-n", 0x02340278, 0x0001ffff, 0xffff0000, 0x00000000, 0x83, 0x03, 2},
      {0x10, " +c-nz", 0x02340278, 0x5679ffff, 0xffff0234, 0x011a2b3c, 0x83, 0x01, 2},
      {0}
    }
  };

opcode_suite test_rorq1 =
  {
    "rorq c=1", 63, 16, 0x64,
    {
      // test with carry set -> no positive result possible
      {0, 0x6a, 0xc0, "rorq1 q"},
      {2, 0x66, 0x81, "rorq1 $nn"},
      {1, 0x6e, 0x82, "rorq1 $nnnn"},
      {2, 0x76, 0x90, "rorq1 $nn,x"},
      {1, 0x7e, 0xa0, "rorq1 $nnnn,x"},
      {0, 0},
    },
    {
      // implied Q
      {0x40, " +n-zc", 0x42345678, 0xf2340078, 0xffffffff, 0xa11a2b3c, 0x83, 0x80, 0},
      {0x40, " +nc-z", 0x23456789, 0xd2340078, 0xffffffff, 0x91a2b3c4, 0x83, 0x81, 0},
      // test with x=0
      {0x11, " +n-zc", 0x02340078, 0x42345678, 0xffffffff, 0xa11a2b3c, 0x83, 0x80, 0},
      {0x11, " +nc-z", 0x02340078, 0x23456789, 0xffffffff, 0x91a2b3c4, 0x83, 0x81, 0},
      // test with x=2, offset 2
      {0x10, " +n-zc", 0x02340278, 0x5678ffff, 0xffff4234, 0xa11a2b3c, 0x83, 0x80, 2},
      {0x10, " +nc-z", 0x02340278, 0x6789ffff, 0xffff2345, 0x91a2b3c4, 0x83, 0x81, 2},
      {0}
    }
  };

/*
42422A   ROLQ
424226   ROLQ $nn
42422E   ROLQ $nnnn
424236   ROLQ $nn,X
42423E   ROLQ $nnnn,X
*/

opcode_suite test_rolq0 =
  {
    "rolq c=0", 63, 16, 0x64,
    {
      // test with carry clear
      {0, 0x2A, 0x40, "rolq0 q"},
      {2, 0x26, 0x01, "rolq0 $nn"},
      {1, 0x2e, 0x02, "rolq0 $nnnn"},
      {2, 0x36, 0x10, "rolq0 $nn,x"},
      {1, 0x3e, 0x20, "rolq0 $nnnn,x"},
      {0, 0},
    },
    {
      // implied Q
      {0x40, " -nzc",  0x02c45678, 0xe2340078, 0x00000000, 0x0588acf0, 0x83, 0x00, 0},
      {0x40, " +zc-n", 0x80000000, 0xd2340078, 0x00000000, 0x00000000, 0x83, 0x03, 0},
      {0x40, " +c-nz", 0x82345679, 0xc2340078, 0x00000000, 0x0468acf2, 0x83, 0x01, 0},
      {0x40, " +n-zc", 0x7f345679, 0xb2340078, 0x00000000, 0xfe68acf2, 0x83, 0x80, 0},
      {0x40, " +nc-z", 0xfedcba98, 0xa2340078, 0x00000000, 0xfdb97530, 0x83, 0x81, 0},
      // tests with x=0
      {0x33, " -nzc",  0x02340078, 0x02c45678, 0x00000000, 0x0588acf0, 0x83, 0x00, 0},
      {0x33, " +zc-n", 0x02340078, 0x80000000, 0x00000000, 0x00000000, 0x83, 0x03, 0},
      {0x33, " +c-nz", 0x02340078, 0x82345679, 0x00000000, 0x0468acf2, 0x83, 0x01, 0},
      {0x33, " +n-zc", 0x02340078, 0x7f345679, 0x00000000, 0xfe68acf2, 0x83, 0x80, 0},
      {0x33, " +nc-z", 0x02340078, 0xfedcba98, 0x00000000, 0xfdb97530, 0x83, 0x81, 0},
      // test with x=2, offset 2
      {0x30, " -nzc",  0x02340278, 0x56780000, 0x000002c4, 0x0588acf0, 0x83, 0x00, 2},
      {0x30, " +zc-n", 0x02340278, 0x0000ffff, 0xffff8000, 0x00000000, 0x83, 0x03, 2},
      {0x30, " +c-nz", 0x02340278, 0x5679dddd, 0xeeee8234, 0x0468acf2, 0x83, 0x01, 2},
      {0x30, " +n-zc", 0x02340278, 0x5679abcd, 0x34567f34, 0xfe68acf2, 0x83, 0x80, 2},
      {0x30, " +nc-z", 0x02340278, 0xba984567, 0xabcdfedc, 0xfdb97530, 0x83, 0x81, 2},
      {0}
    }
  };

opcode_suite test_rolq1 =
  {
    "rolq c=1", 63, 16, 0x64,
    {
      // test with carry set
      {0, 0x2A, 0xc0, "rolq1 q"},
      {2, 0x26, 0x81, "rolq1 $nn"},
      {1, 0x2e, 0x82, "rolq1 $nnnn"},
      {2, 0x36, 0x90, "rolq1 $nn,x"},
      {1, 0x3e, 0xa0, "rolq1 $nnnn,x"},
      {0, 0},
    },
    {
      // implied Q
      {0x40, " -nzc",  0x02c45678, 0x02340078, 0x00000000, 0x0588acf1, 0x83, 0x00, 0},
      {0x40, " +c-nz", 0x82345679, 0x02340078, 0x00000000, 0x0468acf3, 0x83, 0x01, 0},
      {0x40, " +n-zc", 0x7f345679, 0x02340078, 0x00000000, 0xfe68acf3, 0x83, 0x80, 0},
      {0x40, " +nc-z", 0xfedcba98, 0x02340078, 0x00000000, 0xfdb97531, 0x83, 0x81, 0},
      // tests with x=0
      {0x33, " -nzc",  0x02340078, 0x02c45678, 0x00000000, 0x0588acf1, 0x83, 0x00, 0},
      {0x33, " +c-nz", 0x02340078, 0x82345679, 0x00000000, 0x0468acf3, 0x83, 0x01, 0},
      {0x33, " +n-zc", 0x02340078, 0x7f345679, 0x00000000, 0xfe68acf3, 0x83, 0x80, 0},
      {0x33, " +nc-z", 0x02340078, 0xfedcba98, 0x00000000, 0xfdb97531, 0x83, 0x81, 0},
      // test with x=2, offset 2
      {0x30, " -nzc",  0x02340278, 0x56780000, 0x000002c4, 0x0588acf1, 0x83, 0x00, 2},
      {0x30, " +c-nz", 0x02340278, 0x5679dddd, 0xeeee8234, 0x0468acf3, 0x83, 0x01, 2},
      {0x30, " +n-zc", 0x02340278, 0x5679abcd, 0x34567f34, 0xfe68acf3, 0x83, 0x80, 2},
      {0x30, " +nc-z", 0x02340278, 0xba984567, 0xabcdfedc, 0xfdb97531, 0x83, 0x81, 2},
      {0}
    }
  };

// defintion of all the test suites to run
opcode_suite *suites[] = {
  &test_ldq,
  &test_stq,
  &test_adcq,
  &test_sbcq,
  &test_cmpq,
  &test_bitq,
  &test_deq,
  &test_inq,
  &test_andq,
  &test_orq,
  &test_eorq,
  &test_lsrq,
  &test_asrq,
  &test_aslq,
  &test_rorq0,
  &test_rorq1,
  &test_rolq0,
  &test_rolq1,
  0x0
};

/* code snippet
   SEI
   CLD
   ; preload registers with garbage
   LDA #$fa
   LDX #$5f
   LDY #$af
   LDZ #$f5
   ; Load test value to Q, LDQ $0380
   NEG
   NEG
   LDA $0380
   ; placeholder for 32 bit addressing and adcq/sbcq
   CLC
   CLC
   ; opcode (LDQ)
   NEG
   NEG
   LDA $nnnn
   ; store flags after our op!
   PHP
   ; Store result back
   ; not suing STQ, because we want just test the op above
   STA $038C
   STX $038D
   STY $038E
   STZ $038F
   ; and the flags
   PLA
   STA $0390
   RTS
 */
unsigned char code_snippet[64] = {
    0x78, 0xd8, 0xa9, 0xfa, 0xa2, 0x5f, 0xa0, 0xaf, 0xa3, 0xf5,
    // LDQ Val1 from 0380
    0x42, 0x42, 0xad, 0x80, 0x03,
    // Test Opcode at 0384 (with padding) -- this is the part that gets changed every test
    0x18, 0x18, 0x42, 0x42, 0x8d, 0x84, 0x03,
    // store Q to 038C and Flags to 0390
    0x08, 0x8d, 0x8c, 0x03, 0x8e, 0x8d, 0x03, 0x8c, 0x8e, 0x03,
    0x9c, 0x8f, 0x03, 0x68, 0x8d, 0x90, 0x03, 0x60, 0x00
};

unsigned char issue = 0, sub = 0, failed;
char msg[81];
unsigned short i, line = 0, total=0, total_failed=0;
unsigned char concmsg[81] = "", flagstr[11]="", failstr[3]="", testname[21]="";

// Tape Buffer
unsigned char *code_buf = (unsigned char*)0x340;

unsigned long result_q;
unsigned char result_f, status;

unsigned int reslo, reshi;

// save vic state for restoring later...
unsigned char state[13];

void init_mega65(void) {
  // Fast CPU, M65 IO
  POKE(0, 65);
  POKE(0xD02F, 0x47);
  POKE(0xD02F, 0x53);

  // Stop all DMA audio first
  POKE(0xD720, 0);
  POKE(0xD730, 0);
  POKE(0xD740, 0);
  POKE(0xD750, 0);
}

void h640_text_mode(void)
{
  // save state
  state[0] = PEEK(0xD018);
  state[1] = PEEK(0xD054);
  state[2] = PEEK(0xD031);
  state[3] = PEEK(0xD016);
  state[4] = PEEK(0xD058);
  state[5] = PEEK(0xD059);
  state[6] = PEEK(0xD05E);
  state[7] = PEEK(0xD060);
  state[8] = PEEK(0xD061);
  state[9] = PEEK(0xD062);
  state[10] = PEEK(0xD05D);
  state[11] = PEEK(0xD020);
  state[12] = PEEK(0xD021);

  // lower case
  POKE(0xD018, 0x16);

  // Normal text mode
  POKE(0xD054, 0x00);
  // H640, fast CPU, extended attributes
  POKE(0xD031, 0xE0);
  // Adjust D016 smooth scrolling for VIC-III H640 offset
  POKE(0xD016, 0xC9);
  // 640x200 16bits per char, 16 pixels wide per char
  // = 640/16 x 16 bits = 80 bytes per row
  POKE(0xD058, 80);
  POKE(0xD059, 80 / 256);
  // Draw 80 chars per row
  POKE(0xD05E, 80);
  // Put 2KB screen at $C000
  POKE(0xD060, 0x00);
  POKE(0xD061, 0xc0);
  POKE(0xD062, 0x00);

  lfill(0xc000, 0x20, 2000);
  
  // Clear colour RAM
  lfill(0xff80000L, 0x0E, 2000);

  // Disable hot registers
  POKE(0xD05D, PEEK(0xD05D) & 0x7f);

  // light grey background, black frame
  POKE(0xD020, 0);
  POKE(0xD021, 0x0b);
}

void restore_graphics(void) {
  // restore saved state
  POKE(0xD05D, state[10]);
  POKE(0xD018, state[0]);
  POKE(0xD054, state[1]);
  POKE(0xD031, state[2]);
  POKE(0xD016, state[3]);
  POKE(0xD058, state[4]);
  POKE(0xD059, state[5]);
  POKE(0xD05E, state[6]);
  POKE(0xD060, state[7]);
  POKE(0xD061, state[8]);
  POKE(0xD062, state[9]);
  POKE(0xD020, state[11]);
  POKE(0xD021, state[12]);
}

void print_text80(unsigned char x, unsigned char y, unsigned char colour, char* msg)
{
  unsigned char char_code;
  unsigned short pixel_addr = 0xC000 + x + y*80;

  if (y > 24) return; // don't print beyond screen

  while (*msg) {
    char_code = *msg;
    if (*msg >= 0xc0 && *msg <= 0xe0)
      char_code = *msg - 0x80;
    else if (*msg >= 0x40 && *msg <= 0x60)
      char_code = *msg - 0x40;
    else if (*msg >= 0x60 && *msg <= 0x7A)
      char_code = *msg - 0x20;
    POKE(pixel_addr + 0, char_code);
    lpoke(0xff80000L - 0xc000 + pixel_addr, colour);
    msg++;
    pixel_addr += 1;
  }
}

unsigned char keybuffer(unsigned char wait) {
  unsigned char key = 0;
  // clear keyboard buffer
  while (PEEK(0xD610))
    POKE(0xD610, 0);

  if (wait) {
    while ((key = PEEK(0xD610)) == 0);
    POKE(0xD610, 0);
  }

  return key;
}

void format_flags(unsigned char flags)
{
    flagstr[0] = '[';
    if (flags & 0x80)
        flagstr[1] = 'n';
    else
        flagstr[1] = '.';
    if (flags & 0x40)
        flagstr[2] = 'v';
    else
        flagstr[2] = '.';
    if (flags & 0x20)
        flagstr[3] = 'e';
    else
        flagstr[3] = '.';
    if (flags & 0x10)
        flagstr[4] = 'b';
    else
        flagstr[4] = '.';
    if (flags & 0x08)
        flagstr[5] = 'd';
    else
        flagstr[5] = '.';
    if (flags & 0x04)
        flagstr[6] = 'i';
    else
        flagstr[6] = '.';
    if (flags & 0x02)
        flagstr[7] = 'z';
    else
        flagstr[7] = '.';
    if (flags & 0x01)
        flagstr[8] = 'c';
    else
        flagstr[8] = '.';
    flagstr[9] = ']';
    flagstr[10] = 0x0;
}

void test_opcode(unsigned char issue_num, opcode_suite *suite) {
  // each opcode starts with subissue number 1
  unsigned char j, i, c=0;
  sub = 1;
  failed = 0;

  // Pre-install code snippet
  lcopy((long)code_snippet, (long)code_buf, 50);

  // setup zero page pointers
  // Run each test
  for (j=0; suite->modes[j].opcode; j++) {
    for (i=0; suite->tests[i].apply_mode; i++) {
      if ((suite->modes[j].mode & suite->tests[i].apply_mode)) {
      //if ((suite->modes[j].mode & suite->tests[i].apply_mode)) &&
      //    !(suite->modes[j].mode & 0x40)) { // skip Q implied for now
        c++; total++;
        // Setup input values
        *(unsigned long*)0x380 = suite->tests[i].val1;
        *(unsigned long*)0x384 = suite->tests[i].val2;
        *(unsigned long*)0x388 = suite->tests[i].val3;
        // preset return value with pattern
        *(unsigned long*)0x38c = 0xb1eb1bbe;
        // setup zero page pointers - we use part of the Basic FAC range of addresses here
        *(unsigned long*)0x60 = 0x00000380;
        *(unsigned long*)0x64 = 0x00000384;

        // change code for test
        if (suite->modes[j].mode & 0x80)
          code_buf[suite->offset2-1] = 0x38;
        else 
          code_buf[suite->offset2-1] = 0x18;

        // this should be optimized!!!
        if (suite->modes[j].mode & 0x01) { // $nn
          code_buf[suite->offset2]   = (suite->modes[j].mode & 0x80)?0x38:0x18;
          code_buf[suite->offset2+1] = 0x42;
          code_buf[suite->offset2+2] = 0x42;
          code_buf[suite->offset2+3] = suite->modes[j].opcode;
          code_buf[suite->offset2+4] = suite->basepage;
          code_buf[suite->offset2+5] = 0xea;
          *(unsigned long*)0x60 = suite->tests[i].val1;
          *(unsigned long*)0x64 = suite->tests[i].val2;
          *(unsigned long*)0x68 = suite->tests[i].val3;
        } else if (suite->modes[j].mode & 0x02) { // $nnnn
          code_buf[suite->offset2]   = (suite->modes[j].mode & 0x80)?0x38:0x18;
          code_buf[suite->offset2+1] = 0x42;
          code_buf[suite->offset2+2] = 0x42;
          code_buf[suite->offset2+3] = suite->modes[j].opcode;
          code_buf[suite->offset2+4] = 0x84;
          code_buf[suite->offset2+5] = 0x03;
        } else if (suite->modes[j].mode & 0x04) { // ($nn)
          code_buf[suite->offset2]   = (suite->modes[j].mode & 0x80)?0x38:0x18;
          code_buf[suite->offset2+1] = 0x42;
          code_buf[suite->offset2+2] = 0x42;
          code_buf[suite->offset2+3] = suite->modes[j].opcode;
          code_buf[suite->offset2+4] = suite->basepage;
          code_buf[suite->offset2+5] = 0xea;
        } else if (suite->modes[j].mode & 0x08) { // [$nn]
          code_buf[suite->offset2]   = 0x42;
          code_buf[suite->offset2+1] = 0x42;
          code_buf[suite->offset2+2] = 0xea;
          code_buf[suite->offset2+3] = suite->modes[j].opcode;
          code_buf[suite->offset2+4] = suite->basepage;
          code_buf[suite->offset2+5] = 0xea;
        } else if (suite->modes[j].mode & 0x10) { // $nn,x
          code_buf[suite->offset2]   = (suite->modes[j].mode & 0x80)?0x38:0x18;
          code_buf[suite->offset2+1] = 0x42;
          code_buf[suite->offset2+2] = 0x42;
          code_buf[suite->offset2+3] = suite->modes[j].opcode;
          code_buf[suite->offset2+4] = suite->basepage;
          code_buf[suite->offset2+5] = 0xea;
          *(unsigned long*)0x60 = suite->tests[i].val1;
          *(unsigned long*)0x64 = suite->tests[i].val2;
          *(unsigned long*)0x68 = suite->tests[i].val3;
        } else if (suite->modes[j].mode & 0x20) { // $nnnn,x
          code_buf[suite->offset2]   = (suite->modes[j].mode & 0x80)?0x38:0x18;
          code_buf[suite->offset2+1] = 0x42;
          code_buf[suite->offset2+2] = 0x42;
          code_buf[suite->offset2+3] = suite->modes[j].opcode;
          code_buf[suite->offset2+4] = 0x84;
          code_buf[suite->offset2+5] = 0x03;
        } else if (suite->modes[j].mode & 0x40) { // accumulator
          code_buf[suite->offset2]   = (suite->modes[j].mode & 0x80)?0x38:0x18;
          code_buf[suite->offset2+1] = 0x42;
          code_buf[suite->offset2+2] = 0x42;
          code_buf[suite->offset2+3] = suite->modes[j].opcode;
          code_buf[suite->offset2+4] = 0xea;
          code_buf[suite->offset2+5] = 0xea;
        }
        __asm__("jsr $0340");

        // read results
        if (suite->modes[j].rmw == 1)
          result_q = *(unsigned long*)(0x384 + suite->tests[i].read_offset);
        else if (suite->modes[j].rmw == 2)
          result_q = *(unsigned long*)(suite->basepage + suite->tests[i].read_offset);
        else
          result_q = *(unsigned long*)0x38c;
        result_f = *(unsigned char*)0x390;

        reslo = (unsigned int) (result_q&0xffff);
        reshi = (unsigned int) (result_q>>16);
        format_flags(result_f);
        // check return values
        snprintf(testname, 20, "%s%s", suite->modes[j].name, suite->tests[i].name);
        if (result_q != suite->tests[i].expected && (result_f&suite->tests[i].flag_mask) != suite->tests[i].flag_val) {
            snprintf(concmsg, 45, "%-.20s q=$%04x%04x %s", testname, reshi, reslo, flagstr);
            failstr[0] = 'q';
            failstr[1] = 'f';
            failstr[2] = 0x0;
            status = TEST_FAIL;
        } else if (result_q != suite->tests[i].expected) {
            snprintf(concmsg, 45, "%-.20s q=$%04x%04x", testname, reshi, reslo);
            failstr[0] = 'q';
            failstr[1] = 0x0;
            status = TEST_FAIL;
        } else if ((result_f&suite->tests[i].flag_mask) != suite->tests[i].flag_val) {
            snprintf(concmsg, 45, "%-.20s %s", testname, flagstr);
            failstr[0] = 'f';
            failstr[1] = 0x0;
            status = TEST_FAIL;
        } else {
            snprintf(concmsg, 45, "%-.20s", testname);
            failstr[0] = 0x0;
            status = TEST_PASS;
        }

        if (status == TEST_FAIL) {
          snprintf(msg, 80, "#%02d*%02d - fail(%-2s) - %-20.20s q=$%04x%04x %s", issue_num, sub, failstr, testname, reshi, reslo, flagstr);
          print_text80(0, line++, 1, msg);
          failed++; total_failed++;
        }

        unit_test_set_current_name(concmsg);
        unit_test_report(issue_num, sub++, status);
      }
    }
  }

  if (failed > 0)
    snprintf(msg, 80, "#%02d - %-4s - %d/%d tests failed", issue_num, suite->name, failed, c);
  else
    snprintf(msg, 80, "#%02d - %-4s - %d tests passed", issue_num, suite->name, c);
  print_text80(0, line++, failed>0?2:5, msg);

  if (failed > 0)
    snprintf(msg, 80, "%s - %d/%d tests failed", suite->name, failed, c);
  else
    snprintf(msg, 80, "%s - %d tests passed", suite->name, c);
  unit_test_set_current_name(msg);
  unit_test_report(issue_num, 99, failed>0?TEST_FAIL:TEST_PASS);
}

void main(void)
{
  asm("sei");

  init_mega65();
  h640_text_mode();
  keybuffer(0);

  print_text80(0, line++, 7, "Q-Opcode Test Suite");
  unit_test_setup("q-opcode test", 0);

  for (issue=0; suites[issue]; issue++)
    test_opcode(issue+1, suites[issue]);

  if (total_failed > 0)
    snprintf(msg, 80, "Total %d/%d tests failed                                                   ", total_failed, total);
  else
    snprintf(msg, 80, "Total %d tests passed                                                      ", total);
  print_text80(0, (line<23)?line++:23, total_failed>0?2:5, msg);

  if (total_failed > 0)
    snprintf(msg, 80, "total - %d/%d tests failed", total_failed, total);
  else
    snprintf(msg, 80, "total - %d tests passed", total);
  unit_test_set_current_name(msg);
  unit_test_report(0, 99, failed>0?TEST_FAIL:TEST_PASS);

  unit_test_set_current_name("q-opcode test");
  unit_test_report(0, 100, TEST_DONEALL);
  print_text80(0, (line<24)?line:24, 7, "Q-Opcode Test Finished                                                       ");

  keybuffer(1);
  restore_graphics();
}
