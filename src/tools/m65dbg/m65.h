#ifndef M65_H
#define M65_H

#include "serial.h"

size_t serialport_read(int fd, uint8_t * buffer, size_t size);
int serialport_write(int fd, uint8_t * buffer, size_t size);
long long gettime_us();
int monitor_sync(void);
int get_pc(void);
int stuff_keybuffer(char *s);
unsigned char mega65_peek(unsigned int addr);
unsigned long long gettime_ms();
int stop_cpu(void);
int restart_hyppo(void);
void print_spaces(FILE *f,int col);
int fetch_ram(unsigned long address,unsigned int count,unsigned char *buffer);
int fetch_ram_cacheable(unsigned long address,unsigned int count,unsigned char *buffer);
int dump_bytes(int col, char *msg,unsigned char *bytes,int length);
int fetch_ram_invalidate(void);
int start_cpu(void);
void timestamp_msg(char *msg);
int detect_mode(void);

#endif // M65_H
