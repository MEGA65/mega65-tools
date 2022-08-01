#ifndef SERIAL_H
#define SERIAL_H

#include <stdbool.h>

#ifdef WINDOWS
#define PORT_TYPE HANDLE
#else
#define PORT_TYPE int
#endif

/**
 * @brief Writes a string to the serial port.
 *
 * Also writes a newline if the string does not end with one.
 *
 * @param string ptr to null-terminated string to write
 */
void serialWrite(char *string);

/**
 * @brief Reads serial data up to the command prompt.
 *
 * The routine will read up until the next '.' prompt. It
 * crops out the echo of the command and the prompt.
 *
 * This waits until a dot prompt is detected, with a short timeout.
 *
 * @param buf ptr to input buffer
 * @param bufsize size of input buffer
 * @return true if read to the next '.' prompt, otherwise needs to be called
 *     again to read more
 */
bool serialRead(char *buf, int bufsize);

/**
 * @brief Sets the transmission rate.
 *
 * @param fastmode true for 4000000, false for 2000000.
 */
void serialBaud(bool fastmode);

/**
 * @brief Flushes the read buffer.
 */
void serialFlush(void);

#endif
