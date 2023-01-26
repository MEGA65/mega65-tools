#ifndef LOG_H
#define LOG_H

#include <stdio.h>

// log level defines
#define LOG_DISABLE -1
#define LOG_CRIT 0
#define LOG_ERROR 1
#define LOG_WARN 2
#define LOG_NOTE 3
#define LOG_INFO 4
#define LOG_DEBUG 5

/*
 * log_parse_level(levelarg)
 *
 * parses levelarg for a number or log_level_name_low part to
 * convert it to a log_level number
 */
int log_parse_level(char *levelarg);

/*
 * log_setup(outfile, level)
 *
 * sets the output FILE for all logging and the
 * minimum log level that gets logged.
 */
void log_setup(FILE *outfile, const int level);

/*
 * log_raiselevel(level)
 *
 * sets minimum log_level to level, if the
 * current log_level is less than level
 */
void log_raiselevel(const int level);

/*
 * log_concat(message, ...)
 *
 * used to concat longer log messages.
 * To start, call with NULL as messages. Each consequent call
 * will append to the global log_temp_message.
 *
 * use a log_LEVEL function with message NULL to use the
 * log_temp_message.
 */
void log_concat(char *message, ...);

/*
 * log_LEVEL(message, ...)
 *
 * format and log message with level LEVEL.
 */
void log_crit(const char *message, ...);
void log_error(const char *message, ...);
void log_warn(const char *measage, ...);
void log_note(const char *measage, ...);
void log_info(const char *message, ...);
void log_debug(const char *message, ...);

#endif