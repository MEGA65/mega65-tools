#ifndef M65COMMON_H
#define M65COMMON_H

#include <stdint.h>

#ifdef WINDOWS
#include <windows.h>
#define SSIZE_T SIZE_T

#define WINPORT_TYPE_INVALID -1
#define WINPORT_TYPE_FILE 0
#define WINPORT_TYPE_SOCK 1
typedef struct {
  int type; // 0 = file, 1 = socket
  HANDLE fdfile;
  SOCKET fdsock;
} WINPORT;

#define PORT_TYPE WINPORT

SSIZE_T do_serial_port_read(WINPORT port, uint8_t *buffer, size_t size, const char *func, const char *file, const int line);
int do_serial_port_write(WINPORT port, uint8_t *buffer, size_t size, const char *func, const char *file, const int line);

#else
#define SSIZE_T size_t
#define PORT_TYPE int
size_t do_serial_port_read(int fd, uint8_t *buffer, size_t size, const char *func, const char *file, const int line);
int do_serial_port_write(int fd, uint8_t *buffer, size_t size, const char *func, const char *file, const int line);
#endif

#define serialport_read(fdport, buffer, size) do_serial_port_read(fdport, buffer, size, __func__, __FILE__, __LINE__)
#define serialport_write(fdport, buffer, size) do_serial_port_write(fdport, buffer, size, __func__, __FILE__, __LINE__)
#define slow_write(fdport, buffer, size) do_slow_write(fdport, buffer, size, __func__, __FILE__, __LINE__)
#define slow_write_safe(fdport, buffer, size) do_slow_write_safe(fdport, buffer, size, __func__, __FILE__, __LINE__)

#define FAT32_MIN_END_OF_CLUSTER_MARKER 0xffffff8

int fetch_ram(unsigned long address, unsigned int count, unsigned char *buffer);
int push_ram(unsigned long address, unsigned int count, unsigned char *buffer);
int mega65_poke(unsigned int addr, unsigned char value);
unsigned char mega65_peek(unsigned int addr);
int dump_bytes(int col, char *msg, unsigned char *bytes, int length);
void print_spaces(FILE *f, int col);
int restart_hyppo(void);
int load_file(char *filename, int load_addr, int patchHyppo);
int start_cpu(void);
int fake_stop_cpu(void);
int real_stop_cpu(void);
unsigned long long gettime_ms();
void purge_input(void);
void wait_for_prompt(void);
long long gettime_us();
int do_slow_write_safe(PORT_TYPE fd, char *d, int l, const char *func, const char *file, const int line);
int do_slow_write(PORT_TYPE fd, char *d, int l, const char *func, const char *file, const int line);
void timestamp_msg(char *msg);
int stuff_keybuffer(char *s);
int read_and_print(PORT_TYPE fd);
int monitor_sync(void);
int rxbuff_detect(void);
int get_pc(void);
int in_hypervisor(void);
int breakpoint_set(int pc);
int breakpoint_wait(void);
int push_ram(unsigned long address, unsigned int count, unsigned char *buffer);
int fetch_ram(unsigned long address, unsigned int count, unsigned char *buffer);
int fetch_ram_invalidate(void);
int fetch_ram_cacheable(unsigned long address, unsigned int count, unsigned char *buffer);
int detect_mode(void);
void print_error(const char *context);
#ifdef WINDOWS
HANDLE open_serial_port(const char *device, uint32_t baud_rate);
#else
void set_serial_speed(int fd, int serial_speed);
#endif
void open_the_serial_port(char *serial_port);
void close_communication_port(void);
int switch_to_c64mode(void);
PORT_TYPE open_tcp_port(char *portname);
void close_tcp_port(PORT_TYPE localfd);
void close_default_tcp_port(void);
void do_write(PORT_TYPE localfd, char *str);
int do_read(PORT_TYPE localfd, char *str, int max);
void progress_to_RTI(void);

#ifdef WINDOWS
#define bzero(b, len) (memset((b), '\0', (len)), (void)0)
#define bcopy(b1, b2, len) (memmove((b2), (b1), (len)), (void)0)
void do_usleep(__int64 usec);
#else
#include <termios.h>
void do_usleep(unsigned long usec);
int stricmp(const char *a, const char *b);
#endif

#ifdef __APPLE__
#include <sys/ioctl.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/serial/IOSerialKeys.h>
#include <IOKit/serial/ioss.h>
#include <IOKit/IOBSD.h>
#endif

extern time_t start_time;
extern PORT_TYPE fd;
extern int serial_speed;
extern int saw_c64_mode;
extern int saw_c65_mode;
extern int saw_openrom;
extern int xemu_flag;

#endif // M65COMMON_H
