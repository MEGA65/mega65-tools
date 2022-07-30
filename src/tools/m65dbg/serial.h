/* vim: set expandtab shiftwidth=2 tabstop=2: */

/**
 * serial.h
 */

#include <stdbool.h>

#ifdef WINDOWS
#define PORT_TYPE HANDLE
#else
#define PORT_TYPE int
#endif

bool serialOpen(char* portName);
bool serialClose(void);
void serialWrite(char* string);
bool serialRead(char* buf, int bufsize);
void serialBaud(bool fastmode);
void serialFlush(void);
