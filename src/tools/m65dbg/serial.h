/**
 * serial.h
 */

#include <stdbool.h>

bool serialOpen(char* portName);
bool serialClose(void);
void serialWrite(char* string);
bool serialRead(char* buf, int bufsize);
