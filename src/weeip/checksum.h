
#ifndef __CHECKSUMH__
#define __CHECKSUMH__
#include "task.h"

typedef union {
   uint16_t u;
   byte_t b[2];
} chks_t;
extern chks_t chks;

extern void add_checksum(uint16_t v);
extern void ip_checksum(localbuffer_t p, uint16_t t);
#define checksum_init() {chks.u = 0;}
#define checksum_result() (~chks.u)
#endif
