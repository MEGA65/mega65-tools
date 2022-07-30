#ifndef SERIAL_H
#define SERIAL_H

#include <stdbool.h>

#ifdef WINDOWS
#define PORT_TYPE HANDLE
#else
#define PORT_TYPE int
#endif

bool serialOpen(char *portName);
bool serialClose(void);
void serialWrite(char *string);
bool serialRead(char *buf, int bufsize);
void serialBaud(bool fastmode);
void serialFlush(void);

/** TODO: m65common defines this, I guess: extern int fd; **/

extern int do_ftp(char *bitstream);

#endif
