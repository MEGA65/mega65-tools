#ifndef LOG_H
#define LOG_H

#define LOG_CRIT 0
#define LOG_ERROR 1
#define LOG_WARN 2
#define LOG_NOTE 3
#define LOG_INFO 4
#define LOG_DEBUG 5

int log_parse_level(char *levelarg);
void log_setup(FILE *outfile, const int level);
void log_raiselevel(const int level);
void log_concat(char *message, ...);
void log_crit(const char *message, ...);
void log_error(const char *message, ...);
void log_warn(const char *measage, ...);
void log_note(const char *measage, ...);
void log_info(const char *message, ...);
void log_debug(const char *message, ...);

#endif