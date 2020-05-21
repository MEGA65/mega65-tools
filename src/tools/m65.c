/*
  Load the specified program into memory on the C65GS via the serial monitor.

  We add some convenience features:

  1. If an optional file name is provided, then we stuff the keyboard buffer
  with the LOAD command.  We check if we are in C65 mode, and if so, do GO64
  (look for reversed spaces at $0800 for C65 ROM detection).  Keyboard buffer @ $34A, 
  buffer length @ $D0 in C65 mode, same as C128.  Then buffer is $277 in C64
  mode, buffer length @ $C6 in C64 mode.
  
  2. If an optional bitstream file is provided, then we use fpgajtag to load
  the bitstream via JTAG.  fpgajtag is now compiled internally to this program.

  Copyright (C) 2014-2020 Paul Gardner-Stephen
  Portions Copyright (C) 2013 Serval Project Inc.
 
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.
 
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <strings.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <pthread.h>

#define PNG_DEBUG 3
#include <png.h>

#ifdef WINDOWS
#include <windows.h>
#else
#include <termios.h>
#endif

#ifdef WINDOWS
#define PORT_TYPE HANDLE
SSIZE_T serialport_read(HANDLE port, uint8_t * buffer, size_t size);
int serialport_write(HANDLE port, uint8_t * buffer, size_t size);
#else
#define PORT_TYPE int
size_t serialport_read(int fd, uint8_t * buffer, size_t size);
int serialport_write(int fd, uint8_t * buffer, size_t size);
#endif

#define bzero(b,len) (memset((b), '\0', (len)), (void) 0)  
#define bcopy(b1,b2,len) (memmove((b2), (b1), (len)), (void) 0)

#ifdef WINDOWS
FILE iobs[3];

FILE *__imp___acrt_iob_func(void)
{
  iobs[0]=*stdin;
  iobs[1]=*stdout;
  iobs[2]=*stderr;
  return iobs;
}
#endif

#ifdef APPLE
static const int B1000000 = 1000000;
static const int B1500000 = 1500000;
static const int B2000000 = 2000000;
static const int B4000000 = 4000000;
#endif
time_t start_time=0;

int osk_enable=0;

int not_already_loaded=1;

int halt=0;

int usedk=0;


// 0 = old hard coded monitor, 1= Kenneth's 65C02 based fancy monitor
int new_monitor=0;

int viciv_mode_report(unsigned char *viciv_regs);

int process_char(unsigned char c,int live);
int process_line(char *line,int live);
int process_waiting(PORT_TYPE fd);
int fpgajtag_main(char *bitstream,char *serialport);
void init_fpgajtag(const char *serialno, const char *filename, uint32_t file_idcode);
int xilinx_boundaryscan(char *xdc,char *bsdl,char *sensitivity);
void set_vcd_file(char *name);
void do_exit(int retval);

void usage(void)
{
  fprintf(stderr,"MEGA65 cross-development tool for booting the MEGA65 using a custom bitstream and/or HICKUP file.\n");
  fprintf(stderr,"usage: monitor_load [-l <serial port>] [-s <230400|2000000|4000000>]  [-b <FPGA bitstream> [-v <vivado.exe>] [[-k <hickup file>] [-R romfile] [-U flashmenufile] [-C charromfile]] [-c COLOURRAM.BIN] [-B breakpoint] [-m modeline] [-o] [-d diskimage.d81] [-j] [-J <XDC,BSDL[,sensitivity list]> [-V <vcd file>]] [[-1] [<-t|-T> <text>] [-f FPGA serial ID] [filename]] [-H] [-E|-L] [-Z <flash addr>]\n");
  fprintf(stderr,"  -l - Name of serial port to use, e.g., /dev/ttyUSB1\n");
  fprintf(stderr,"  -s - Speed of serial port in bits per second. This must match what your bitstream uses.\n");
  fprintf(stderr,"       (Older bitstream use 230400, and newer ones 2000000 or 4000000).\n");
  fprintf(stderr,"  -b - Name of bitstream file to load.\n");
  fprintf(stderr,"  -v - The location of the Vivado executable to use for -b on Windows.\n");
  fprintf(stderr,"  -K - Use DK backend for libUSB, if available\n");
  fprintf(stderr,"  -k - Name of hickup file to forcibly use instead of the HYPPO in the bitstream.\n");
  fprintf(stderr,"       NOTE: You can use bitstream and/or HYPPO from the Jenkins server by using @issue/tag/hardware\n"
	  "             for the bitstream, and @issue/tag for HYPPO.\n");
  fprintf(stderr,"  -J - Do JTAG boundary scan of attached FPGA, using the provided XDC and BSDL files.\n");
  fprintf(stderr,"       A sensitivity list can also be provided, to restrict the set of signals monitored.\n");
  fprintf(stderr,"       This will likely be required when producing VCD files, as they can only log ~80 signals.\n");
  fprintf(stderr,"  -j   Do JTAG operation(s), and nothing else.\n");
  fprintf(stderr,"  -V - Write JTAG change log to VCD file, instead of to stdout.\n");
  fprintf(stderr,"  -R - ROM file to preload at $20000-$3FFFF.\n");
  fprintf(stderr,"  -U - Flash menu file to preload at $50000-$57FFF.\n");
  fprintf(stderr,"  -C - Character ROM file to preload.\n");
  fprintf(stderr,"  -c - Colour RAM contents to preload.\n");
  fprintf(stderr,"  -4 - Switch to C64 mode before exiting.\n");
  fprintf(stderr,"  -H - Halt CPU after loading ROMs.\n");
  fprintf(stderr,"  -1 - Load as with ,8,1 taking the load address from the program, instead of assuming $0801\n");
  fprintf(stderr,"  -r - Automatically RUN programme after loading.\n");
  fprintf(stderr,"  -m - Set video mode to Xorg style modeline.\n");
  fprintf(stderr,"  -o - Enable on-screen keyboard\n");
  fprintf(stderr,"  -d - Enable virtual D81 access\n");
  fprintf(stderr,"  -p - Force PAL video mode\n");
  fprintf(stderr,"  -n - Force NTSC video mode\n");
  fprintf(stderr,"  -F - Force reset on start\n");
  fprintf(stderr,"  -t - Type text via keyboard virtualisation.\n");
  fprintf(stderr,"  -T - As above, but also provide carriage return\n");
  fprintf(stderr,"  -B - Set a breakpoint on synchronising, and then immediately exit.\n");
  fprintf(stderr,"  -E - Enable streaming of video via ethernet.\n");
  fprintf(stderr,"  -L - Enable streaming of CPU instruction log via ethernet.\n");
  fprintf(stderr,"  -f - Specify which FPGA to reconfigure when calling fpgajtag\n");
  fprintf(stderr,"  -S - Show the text-mode screen\n");
  fprintf(stderr,"  -Z - Zap (reconfigure) FPGA from specified hex address in flash.\n");
  fprintf(stderr,"  filename - Load and run this file in C64 mode before exiting.\n");
  fprintf(stderr,"\n");
  exit(-3);
}

int cpu_stopped=0;

int pal_mode=0;
int ntsc_mode=0;
int reset_first=0;

int boundary_scan=0;
char boundary_xdc[1024]="";
char boundary_bsdl[1024]="";
char jtag_sensitivity[1024]="";

int hyppo_report=0;
unsigned char hyppo_buffer[1024];

int counter  =0;
#ifdef WINDOWS
PORT_TYPE fd=INVALID_HANDLE_VALUE;
#else
PORT_TYPE fd=-1;
#endif

int state=99;
unsigned int name_len,name_lo,name_hi,name_addr=-1;
int do_go64=0;
int do_run=0;
int comma_eight_comma_one=0;
int ethernet_video=0;
int ethernet_cpulog=0;
int virtual_f011=0;
char *d81file=NULL;
char *filename=NULL;
char *romfile=NULL;
char *flashmenufile=NULL;
char *charromfile=NULL;
char *colourramfile=NULL;
FILE *f=NULL;
FILE *fd81=NULL;
char *search_path=".";
char *bitstream=NULL;
char *vivado_exe=NULL;
char *hyppo=NULL;
char *fpga_serial=NULL;
char *serial_port=NULL; // XXX do a better job auto-detecting this
int serial_speed=2000000;
char modeline_cmd[1024]="";
int break_point=-1;
int jtag_only=0;
uint32_t zap_addr;
int zap=0;

int saw_c64_mode=0;
int saw_c65_mode=0;
int hypervisor_paused=0;

int screen_shot=0;
int screen_rows_remaining=0;
int screen_address=0;
int next_screen_address=0;
int screen_line_offset=0;
int screen_line_step=0;
int screen_width=0;
unsigned char screen_line_buffer[256];

char *type_text=NULL;
int type_text_cr=0;

#define READ_SECTOR_BUFFER_ADDRESS 0xFFD6c00
#define WRITE_SECTOR_BUFFER_ADDRESS 0xFFD6c00
int sdbuf_request_addr = 0;
unsigned char sd_sector_buf[512];
int saved_track = 0;
int saved_sector = 0;
int saved_side = 0;

int slow_write(PORT_TYPE fd,char *d,int l)
{
  // UART is at 2Mbps, but we need to allow enough time for a whole line of
  // writing. 100 chars x 0.5usec = 500usec. So 1ms between chars should be ok.
  int i;
#if 0
  printf("Writing ");
  for(i=0;i<l;i++)
    {
      if (d[i]>=' ') printf("%c",d[i]); else printf("[$%02X]",d[i]);
    }
  printf("\n");
#endif
  
  for(i=0;i<l;i++)
    {
      if (serial_speed==4000000) usleep(1000); else usleep(2000);
      int w=serialport_write(fd,(unsigned char *)&d[i],1);
      while (w<1) {
	if (serial_speed==4000000) usleep(500); else usleep(1000);
	w=serialport_write(fd,(unsigned char *)&d[i],1);
      }
    }
  return 0;
}

int slow_write_safe(PORT_TYPE fd,char *d,int l)
{
  // There is a bug at the time of writing that causes problems
  // with the CPU's correct operation if various monitor commands
  // are run when the CPU is running.
  // Stopping the CPU before and then resuming it after running a
  // command solves the problem.
  // The only problem then is if we have a breakpoint set (like when
  // getting ready to load a program), because we might accidentally
  // resume the CPU when it should be stopping.
  // (We can work around this by using the fact that the new UART
  // monitor tells us when a breakpoint has been reached.
  slow_write(fd,"t1\r",3);
  slow_write(fd,d,l);
  if (!cpu_stopped) {
    //    printf("Resuming CPU after writing string\n");
    slow_write(fd,"t0\r",3);
  }
  return 0;
}

// From os.c in serval-dna
long long gettime_us()
{
  long long retVal = -1;

  do 
    {
      struct timeval nowtv;

      // If gettimeofday() fails or returns an invalid value, all else is lost!
      if (gettimeofday(&nowtv, NULL) == -1)
	{
	  break;
	}

      if (nowtv.tv_sec < 0 || nowtv.tv_usec < 0 || nowtv.tv_usec >= 1000000)
	{
	  break;
	}

      retVal = nowtv.tv_sec * 1000000LL + nowtv.tv_usec;
    }
  while (0);

  return retVal;
}

unsigned long long gettime_ms()
{
  struct timeval nowtv;
  // If gettimeofday() fails or returns an invalid value, all else is lost!
  if (gettimeofday(&nowtv, NULL) == -1)
    perror("gettimeofday");
  return nowtv.tv_sec * 1000LL + nowtv.tv_usec / 1000;
}

int stop_cpu(void)
{
  // Stop CPU
  printf("Stopping CPU\n");
  usleep(50000);
  slow_write(fd,"t1\r",3);
  cpu_stopped=1;
  return 0;
}
int start_cpu(void)
{
  // Stop CPU
  printf("Starting CPU\n");
  usleep(50000);
  slow_write(fd,"t0\r",3);
  cpu_stopped=0;
  return 0;
}

int load_file(char *filename,int load_addr,int patchHyppo)
{
  char cmd[1024];

  FILE *f=fopen(filename,"rb");
  if (!f) {
    fprintf(stderr,"Could not open file '%s'\n",filename);
    exit(-2);
  }

  usleep(50000);
  unsigned char buf[65536];
  int max_bytes;
  int byte_limit=32768;
  max_bytes=0x10000-(load_addr&0xffff);
  if (max_bytes>byte_limit) max_bytes=byte_limit;
  int b=fread(buf,1,max_bytes,f);
  while(b>0) {
    if (patchHyppo) {
      printf("patching...\n");
      // Look for BIT $nnnn / BIT $1234, and change to JMP $nnnn to skip
      // all SD card activities
      for(int i=0;i<(b-5);i++)
	{
	  if ((buf[i]==0x2c)
	      &&(buf[i+3]==0x2c)
	      &&(buf[i+4]==0x34)
	      &&(buf[i+5]==0x12)) {
	    fprintf(stderr,"Patching Hyppo @ $%04x to skip SD card and ROM checks.\n",
		    0x8000+i);
	    buf[i]=0x4c;
	  }
	}
    }
    printf("Read to $%04x (%d bytes)\n",load_addr,b);
    fflush(stdout);
    // load_addr=0x400;
    // XXX - The l command requires the address-1, and doesn't cross 64KB boundaries.
    // Thus writing to $xxx0000 requires adding 64K to fix the actual load address
    int munged_load_addr=load_addr;
    if ((load_addr&0xffff)==0x0000) {
      munged_load_addr+=0x10000;
    }
    // The old uart monitor could handle being given a 28-bit address for the end address,
    // but Kenneth's implementation requires it be a 16 bit address.
    // Also, Kenneth's implementation doesn't need the -1, so we need to know which version we
    // are talking to.
    if (new_monitor) 
      sprintf(cmd,"l%x %x\r",load_addr,(load_addr+b)&0xffff);
    else    
      sprintf(cmd,"l%x %x\r",munged_load_addr-1,(munged_load_addr+b-1)&0xffff);
    // printf("  command ='%s'\n",cmd);
    slow_write(fd,cmd,strlen(cmd));
    usleep(1000);
    int n=b;
    unsigned char *p=buf;
    while(n>0) {
      int w=serialport_write(fd,p,n);
      if (w>0) { p+=w; n-=w; } else usleep(1000);
    }
    if (serial_speed==230400) usleep(10000+50*b);
    else if (serial_speed==2000000)
      // 2mbit/sec / 11bits/char (inc space) = ~5.5usec per char
      usleep(5.1*b);
    else
      // 4mbit/sec / 11bits/char (inc space) = ~2.6usec per char
      usleep(2.6*b);
      
    load_addr+=b;

    max_bytes=0x10000-(load_addr&0xffff);
    if (max_bytes>byte_limit) max_bytes=byte_limit;
    b=fread(buf,1,max_bytes,f);	  
  }
  fclose(f);
#ifdef WINDOWS
  fprintf(stderr,"[T+%I64dsec] '%s' loaded.\n",(long long)time(0)-start_time,filename);
#else
  fprintf(stderr,"[T+%lldsec] '%s' loaded.\n",(long long)time(0)-start_time,filename);
#endif
  
  return 0;
}

int restart_hyppo(void)
{
  // Start executing in new hyppo
  if (!halt) {
    printf("Re-Starting CPU in new HYPPO\n");
    usleep(50000);
    slow_write(fd,"g8100\r",6);
    usleep(10000);
    slow_write(fd,"t0\r",3);
    cpu_stopped=0;
  }
  return 0;
}

void print_spaces(FILE *f,int col)
{
  for(int i=0;i<col;i++)
    fprintf(f," ");  
}

int dump_bytes(int col, char *msg,unsigned char *bytes,int length)
{
  print_spaces(stderr,col);
  fprintf(stderr,"%s:\n",msg);
  for(int i=0;i<length;i+=16) {
    print_spaces(stderr,col);
    fprintf(stderr,"%04X: ",i);
    for(int j=0;j<16;j++) if (i+j<length) fprintf(stderr," %02X",bytes[i+j]); else fprintf(stderr,"   ");
    fprintf(stderr,"  ");
    for(int j=0;j<16;j++) if (i+j<length) fprintf(stderr,"%c",(bytes[i+j]>=' '&&bytes[i+j]<0x7c)? bytes[i+j]:'.');
    
    fprintf(stderr,"\n");
  }
  return 0;
}

int first_load=1;
int first_go64=1;

unsigned char viciv_regs[0x100];
int mode_report=0;

int read_and_print(PORT_TYPE fd)
{
  char buff[8192];
  int r=serialport_read(fd,(unsigned char *)buff,8192);
  buff[r]=0;
  printf("%s\n",buff);
  return 0;
}

int stuff_keybuffer(char *s)
{
  int buffer_addr=0x277;
  int buffer_len_addr=0xc6;

  if (saw_c65_mode) {
    buffer_addr=0x2b0;
    buffer_len_addr=0xd0;
  }

  printf("Injecting string '%s' into key buffer at $%04X\n",s,buffer_addr);
  
  char cmd[1024];
  snprintf(cmd,1024,"s%x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\rs%x %d\r",
	   buffer_addr,s[0],s[1],s[2],s[3],s[4],s[5],s[6],s[7],s[8],s[9],
	   buffer_len_addr,(int)strlen(s));
  return slow_write(fd,cmd,strlen(cmd));
}

long long last_virtual_time=0;
int last_virtual_writep=0;
int last_virtual_track=-1;
int last_virtual_sector=-1;
int last_virtual_side=-1;

int virtual_f011_read(int device,int track,int sector,int side)
{
  char cmd[1024];

  long long start=gettime_ms();

#ifdef WINDOWS
  fprintf(stderr,"T+%I64d ms : Servicing hypervisor request for F011 FDC sector read.\n",
	  gettime_ms()-start);
#else
  fprintf(stderr,"T+%lld ms : Servicing hypervisor request for F011 FDC sector read.\n",
	  gettime_ms()-start);
#endif
  fprintf(stderr, "device: %d  track: %d  sector: %d  side: %d\n", device, track, sector, side);

  if(fd81 == NULL) {

    fd81 = fopen(d81file, "rb+");
    if(!fd81) {
      
      fprintf(stderr, "Could not open D81 file: '%s'\n", d81file);
      exit(-1);
    }
  }

  // Only actually load new sector contents if we don't think it is a duplicate request
  if (((gettime_ms()-last_virtual_time)>100)
      ||(last_virtual_writep)
      ||(last_virtual_track!=track)
      ||(last_virtual_sector!=sector)
      ||(last_virtual_side!=side)
      )
    {
      last_virtual_time=gettime_ms();
      last_virtual_track=track;
      last_virtual_sector=sector;
      last_virtual_side=side;
    
      /* read the block */
      unsigned char buf[512];
      int b=-1;
      int physical_sector=( side==0 ? sector-1 : sector+9 );
      int result = fseek(fd81, (track*20+physical_sector)*512, SEEK_SET);
      if(result) {
      
	fprintf(stderr, "Error finding D81 sector %d @ 0x%x\n", result, (track*20+physical_sector)*512);
	exit(-2);
      }
      else {
	b=fread(buf,1,512,fd81);
	fprintf(stderr, " bytes read: %d @ 0x%x\n", b,(track*20+physical_sector)*512);
	if(b==512) {
	
	  //      dump_bytes(0,"The sector",buf,512);
	
	  char cmd[1024];
	
	  /* send block to m65 memory */
	  if (new_monitor) 
	    sprintf(cmd,"l%x %x\r",READ_SECTOR_BUFFER_ADDRESS,
		    (READ_SECTOR_BUFFER_ADDRESS+0x200)&0xffff);
	  else
	    sprintf(cmd,"l%x %x\r",READ_SECTOR_BUFFER_ADDRESS-1,
		    READ_SECTOR_BUFFER_ADDRESS+0x200-1);
	  slow_write(fd,cmd,strlen(cmd));
	  usleep(1000);
	  int n=0x200;
	  unsigned char *p=buf;
	  //	      fprintf(stderr,"%s\n",cmd);
	  //	      dump_bytes(0,"F011 virtual sector data",p,512);
	  while(n>0) {
	    int w=serialport_write(fd,p,n);
	    if (w>0) { p+=w; n-=w; } else usleep(1000);
	  }
#ifdef WINDOWS       
	  printf("T+%I64d ms : Block sent.\n",gettime_ms()-start);
#else	
	  printf("T+%lld ms : Block sent.\n",gettime_ms()-start);
#endif	
	}
      }

    }

  /* signal done/result */
  snprintf(cmd,1024,"sffd3086 %x\n",side);
  slow_write(fd,cmd,strlen(cmd));
  
#ifdef WINDOWS
  printf("T+%I64d ms : Finished V-FDC read.\n",gettime_ms()-start);
#else
  printf("T+%lld ms : Finished V-FDC read.\n",gettime_ms()-start);
#endif
  return 0;
}

typedef struct {
  char mask;    /* char data will be bitwise AND with this */
  char lead;    /* start bytes of current char in utf-8 encoded character */
  uint32_t beg; /* beginning of codepoint range */
  uint32_t end; /* end of codepoint range */
  int bits_stored; /* the number of bits from the codepoint that fits in char */
}utf_t;
 
utf_t * utf[] = {
  /*             mask        lead        beg      end       bits */
  [0] = &(utf_t){0b00111111, 0b10000000, 0,       0,        6    },
  [1] = &(utf_t){0b01111111, 0b00000000, 0000,    0177,     7    },
  [2] = &(utf_t){0b00011111, 0b11000000, 0200,    03777,    5    },
  [3] = &(utf_t){0b00001111, 0b11100000, 04000,   0177777,  4    },
  [4] = &(utf_t){0b00000111, 0b11110000, 0200000, 04177777, 3    },
  &(utf_t){0},
};

// UTF-8 from https://rosettacode.org/wiki/UTF-8_encode_and_decode#C

/* All lengths are in bytes */
int codepoint_len(const uint32_t cp); /* len of associated utf-8 char */
int utf8_len(const char ch);          /* len of utf-8 encoded char */
 
char *to_utf8(const uint32_t cp);
uint32_t to_cp(const char chr[4]);
 
int codepoint_len(const uint32_t cp)
{
  int len = 0;
  for(utf_t **u = utf; *u; ++u) {
    if((cp >= (*u)->beg) && (cp <= (*u)->end)) {
      break;
    }
    ++len;
  }
  if(len > 4) /* Out of bounds */
    exit(1);
 
  return len;
}
 
int utf8_len(const char ch)
{
  int len = 0;
  for(utf_t **u = utf; *u; ++u) {
    if((ch & ~(*u)->mask) == (*u)->lead) {
      break;
    }
    ++len;
  }
  if(len > 4) { /* Malformed leading byte */
    exit(1);
  }
  return len;
}
 
char *to_utf8(const uint32_t cp)
{
  static char ret[5];
  const int bytes = codepoint_len(cp);
 
  int shift = utf[0]->bits_stored * (bytes - 1);
  ret[0] = (cp >> shift & utf[bytes]->mask) | utf[bytes]->lead;
  shift -= utf[0]->bits_stored;
  for(int i = 1; i < bytes; ++i) {
    ret[i] = (cp >> shift & utf[0]->mask) | utf[0]->lead;
    shift -= utf[0]->bits_stored;
  }
  ret[bytes] = '\0';
  return ret;
}

void print_screencode(unsigned char c, int upper_case)
{
  int rev=0;
  if (c&0x80) {
    rev=1; c&=0x7f;
    // Now swap foreground/background
    printf("%c[7m",27);
  }
  if (c>='0'&&c<='9') printf("%c",c);
  else if (c>=0x00&&c<=0x1f) {
    if (upper_case) printf("%c",c+0x40);
    else printf("%c",c+0x60);
  }
  else if (c>=0x20&&c<0x40) printf("%c",c);
  else if ((c>=0x40&&c<=0x5f)&&(!upper_case)) printf("%c",c);
  
  else if (c==0x60) printf("%s",to_utf8(0xA0));
  else if (c==0x61) printf("%s",to_utf8(0x258c));
  else if (c==0x62) printf("%s",to_utf8(0x2584));
  else if (c==0x63) printf("%s",to_utf8(0x2594));
  else if (c==0x64) printf("%s",to_utf8(0x2581));
  else if (c==0x65) printf("%s",to_utf8(0x258e));
  else if (c==0x66) printf("%s",to_utf8(0x2592));
  else if (c==0x67) printf("%s",to_utf8(0x258a));
  else if (c==0x68) printf("%s",to_utf8(0x7f));     // No Unicode equivalent
  else if (c==0x69) printf("%s",to_utf8(0x25e4));
  else if (c==0x6A) printf("%s",to_utf8(0x258a));
  else if (c==0x6B) printf("%s",to_utf8(0x2523));
  else if (c==0x6C) printf("%s",to_utf8(0x2597));
  else if (c==0x6D) printf("%s",to_utf8(0x2517));
  else if (c==0x6E) printf("%s",to_utf8(0x2513));
  else if (c==0x6F) printf("%s",to_utf8(0x2582));

  else if (c==0x60) printf("%s",to_utf8(0xA0));
  else if (c==0x60) printf("%s",to_utf8(0xA0));
  else if (c==0x60) printf("%s",to_utf8(0xA0));
  else if (c==0x60) printf("%s",to_utf8(0xA0));
  else if (c==0x60) printf("%s",to_utf8(0xA0));
  else if (c==0x60) printf("%s",to_utf8(0xA0));
  else if (c==0x60) printf("%s",to_utf8(0xA0));
  else if (c==0x60) printf("%s",to_utf8(0xA0));
  else if (c==0x60) printf("%s",to_utf8(0xA0));
  else if (c==0x60) printf("%s",to_utf8(0xA0));
  else if (c==0x60) printf("%s",to_utf8(0xA0));
  else if (c==0x60) printf("%s",to_utf8(0xA0));
  else if (c==0x60) printf("%s",to_utf8(0xA0));
  else if (c==0x60) printf("%s",to_utf8(0xA0));
  else if (c==0x60) printf("%s",to_utf8(0xA0));
  else if (c==0x60) printf("%s",to_utf8(0xA0));

  else printf("?");

  if (rev) {
    // Reverse off again
    printf("%c[0m",27);
  }
}

void show_hyppo_report(void)
{
  printf("HYPPO status:\n");
  // Buffer starats at $BC00 in HYPPO
  // $BC00 - $BCFF = DOS work area
  // $BD00 - $BDFF = Process Descriptor
  // $BE00 - $BEFF = Stack
  // $BF00 - $BFFF = ZP

  printf("Disk count = $%02x\n",hyppo_buffer[0x001]);
  printf("Default Disk = $%02x\n",hyppo_buffer[0x002]);
  printf("Current Disk = $%02x\n",hyppo_buffer[0x003]);
  printf("Disk Table offset = $%02x\n",hyppo_buffer[0x004]);
  printf("Cluster of current directory = $%02x%02x%02x%02x\n",
	 hyppo_buffer[0x008],hyppo_buffer[0x007],hyppo_buffer[0x006],hyppo_buffer[0x005]);
  printf("opendir_cluster = $%02x%02x%02x%02x\n",
	 hyppo_buffer[0x00c],hyppo_buffer[0x00b],hyppo_buffer[0x00a],hyppo_buffer[0x009]);
  printf("opendir_sector = $%02x\n",hyppo_buffer[0x00d]);
  printf("opendir_entry = $%02x\n",hyppo_buffer[0x00e]);

  // Dirent struct follows:
  // 64 bytes for file name
  printf("dirent structure:\n");
  printf("  Filename = '%s'\n",&hyppo_buffer[0x00f]);
  printf("  Filename len = $%02x\n",hyppo_buffer[0x04f]);
  printf("  Short name = '%s'\n",&hyppo_buffer[0x050]);
  printf("  Start cluster = $%02x%02x%02x%02x\n",
	 hyppo_buffer[0x060],hyppo_buffer[0x05f],hyppo_buffer[0x05e],hyppo_buffer[0x05d]);
  printf("  File length = $%02x%02x%02x%02x\n",
	 hyppo_buffer[0x064],hyppo_buffer[0x063],hyppo_buffer[0x062],hyppo_buffer[0x061]);
  printf("  ATTRIB byte = $%02x\n",hyppo_buffer[0x065]);
  printf("Requested filename len = $%02x\n",hyppo_buffer[0x66]);
  printf("Requested filename = '%s'\n",&hyppo_buffer[0x67]);
  printf("sectorsread = $%02x%02x\n",
	 hyppo_buffer[0xaa],hyppo_buffer[0xa9]);
  printf("bytes_remaining = $%02x%02x%02x%02x\n",
	 hyppo_buffer[0xae],hyppo_buffer[0x0ad],hyppo_buffer[0xac],hyppo_buffer[0xab]);
  printf("current sector = $%02x%02x%02x%02x, ",
	 hyppo_buffer[0xb2],hyppo_buffer[0x0b1],hyppo_buffer[0xb0],hyppo_buffer[0xaf]);
  printf("current cluster = $%02x%02x%02x%02x\n",
	 hyppo_buffer[0xb6],hyppo_buffer[0x0b5],hyppo_buffer[0xb4],hyppo_buffer[0xb3]);
  printf("current sector in cluster = $%02x\n",hyppo_buffer[0xb7]);

  for(int fd=0;fd<4;fd++) {
    int fd_o=0xb8+fd*0x10;
    printf("File descriptor #%d:\n",fd);
    printf("  disk ID = $%02x, ",hyppo_buffer[fd_o+0]);
    printf("  mode = $%02x\n",hyppo_buffer[fd_o+1]);
    printf("  start cluster = $%02x%02x%02x%02x, ",
	   hyppo_buffer[fd_o+5],hyppo_buffer[fd_o+4],hyppo_buffer[fd_o+3],hyppo_buffer[fd_o+2]);
    printf("  current cluster = $%02x%02x%02x%02x\n",
	   hyppo_buffer[fd_o+9],hyppo_buffer[fd_o+8],hyppo_buffer[fd_o+7],hyppo_buffer[fd_o+6]);
    printf("  sector in cluster = $%02x, ",hyppo_buffer[fd_o+10]);
    printf("  offset in sector = $%02x%02x\n",hyppo_buffer[fd_o+12],hyppo_buffer[fd_o+11]);
    //    printf("  file offset = $%02x%02x (x 256 bytes? not used?)\n",
    //	   hyppo_buffer[fd_o+14],hyppo_buffer[fd_o+13]);
  }

  printf("Current file descriptor # = $%02x\n",hyppo_buffer[0xf4]);
  printf("Current file descriptor offset = $%02x\n",hyppo_buffer[0xf5]);
  printf("Dos error code = $%02x\n",hyppo_buffer[0xf6]);
  printf("SYSPART error code = $%02x\n",hyppo_buffer[0xf7]);
  printf("SYSPART present = $%02x\n",hyppo_buffer[0xf8]);

}

int monitor_sync(void)
{
  /* Synchronise with the monitor interface.
     Send #<token> until we see the token returned to us.
  */

  unsigned char read_buff[8192];
  
  // Begin by sending a null command and purging input
  char cmd[8192];
  cmd[0]=0x15; // ^U
  cmd[1]=0x0d; // Carriage return
  usleep(20000); // Give plenty of time for things to settle
  slow_write_safe(fd,cmd,2);
  printf("Wrote empty command.\n");
  usleep(20000); // Give plenty of time for things to settle
  int b=1;
  // Purge input  
  while(b>0) { b=serialport_read(fd,read_buff,8192); }
  printf("Purging input.\n");

  for(int tries=0;tries<10;tries++) {
#ifdef WINDOWS
    snprintf(cmd,1024,"#%08x\r",rand());
#else
    snprintf(cmd,1024,"#%08lx\r",random());
#endif
    //    printf("Writing token: '%s'\n",cmd);
    slow_write_safe(fd,cmd,strlen(cmd));

    for(int i=0;i<10;i++) {
      b=serialport_read(fd,read_buff,8192);
      if (b<0) b=0;
      if (b>8191) b=8191;
      read_buff[b]=0;
      //      if (b>0) dump_bytes(0,"read_data",read_buff,b);
      if (strstr((char *)read_buff,cmd)) {
	printf("Found token. Synchronised with monitor.\n");
	state=99;
	return 0;
      }
      usleep(2000);
    }
  }
  printf("Failed to synchronise with the monitor.\n");
  return 1;
}

int get_pc(void)
{
  /*
    Get current programme counter value of CPU
  */
  slow_write_safe(fd,"r\r",2);
  usleep(50000);
  unsigned char buff[8192];
  int b=serialport_read(fd,buff,8192);
  if (b<0) b=0;
  if (b>8191) b=8191;
  buff[b]=0;
  char *s=strstr((char *)buff,"\n,");
  if (s) return strtoll(&s[6],NULL,16);
}

int fetch_ram(unsigned long address,unsigned int count,unsigned char *buffer)
{
  /* Fetch a block of RAM into the provided buffer.
     This greatly simplifies many tasks.
  */

  unsigned long addr=address;
  unsigned long end_addr;
  char cmd[8192];
  unsigned char read_buff[8192];
  char next_addr_str[8192];
  int ofs=0;

  //  fprintf(stderr,"Fetching $%x bytes @ $%x\n",count,address);
  
  //  monitor_sync();
  while(addr<(address+count)) {
    if ((address+count-addr)<17) {
      snprintf(cmd,8192,"m%X\r",(unsigned int)addr);
      end_addr=addr+0x10;
    } else {
      snprintf(cmd,8192,"M%X\r",(unsigned int)addr);
      end_addr=addr+0x100;
    }
    //    printf("Sending '%s'\n",cmd);
    slow_write_safe(fd,cmd,strlen(cmd));
    while(addr!=end_addr) {
      snprintf(next_addr_str,8192,"\n:%08X:",(unsigned int)addr);
      int b=serialport_read(fd,&read_buff[ofs],8192-ofs);
      if (b<0) b=0;
      if ((ofs+b)>8191) b=8191-ofs;
      //      if (b) dump_bytes(0,"read data",&read_buff[ofs],b);
      read_buff[ofs+b]=0;
      ofs+=b;
      char *s=strstr((char *)read_buff,next_addr_str);
      if (s&&(strlen(s)>=42)) {
	char b=s[42]; s[42]=0;
	if (0) {
	  printf("Found data for $%08x:\n%s\n",
		 (unsigned int)addr,
		 s);
	} 
	s[42]=b;
	for(int i=0;i<16;i++) {
	  char hex[3];
	  hex[0]=s[1+10+i*2+0];
	  hex[1]=s[1+10+i*2+1];
	  hex[2]=0;
	  buffer[addr-address+i]=strtol(hex,NULL,16);
	}
	addr+=16;

	// Shuffle buffer down
	int s_offset=(long)s-(long)read_buff+42;
	bcopy(&read_buff[s_offset],&read_buff[0],8192-(ofs-s_offset));
	ofs-=s_offset;
      }
    }
  }
  if (addr>=(address+count)) {
    //    fprintf(stderr,"Memory read complete at $%lx\n",addr);
    return 0;
  }
  else {
    fprintf(stderr,"ERROR: Could not read requested memory region.\n");
    exit(-1);
    return 1;
  }
}

unsigned char ram_cache[512*1024+255];
unsigned char ram_cache_valids[512*1024+255];
int ram_cache_initialised=0;

int fetch_ram_cacheable(unsigned long address,unsigned int count,unsigned char *buffer)
{
  if (!ram_cache_initialised) {
    ram_cache_initialised=1;
    bzero(ram_cache_valids,512*1024);
    bzero(ram_cache,512*1024);
  }
  if ((address+count)>=(512*1024)) {
    return fetch_ram(address,count,buffer);
  }

  // See if we need to fetch it fresh
  for(int i=0;i<count;i++) {
    if (!ram_cache_valids[address+i]) {
      // Cache not valid here -- so read some data
      printf("."); fflush(stdout);
      //      printf("Fetching $%08x for cache.\n",address);
      fetch_ram(address,256,&ram_cache[address]);
      for(int j=0;j<256;j++) ram_cache_valids[address+j]=1;

      bcopy(&ram_cache[address],buffer,count);
      return 0;
    }
  }

  // It's valid in the cache
  bcopy(&ram_cache[address],buffer,count);
  return 0;
  
}


int detect_mode(void)
{
  /*
    Set saw_c64_mode or saw_c65_mode according to what we can discover. 
    We can look at the C64/C65 charset bit in $D030 for a good clue.
    But we also really want to know that the CPU is in the keyboard 
    input loop for either of the modes, if possible. OpenROMs being
    under development makes this tricky.
  */
  unsigned char mem_buff[8192];
  fetch_ram(0xffd3030,1,mem_buff);
  printf("$D030 = $%02X\n",mem_buff[0]);
  if (mem_buff[0]==0x64) {
    // Probably C65 mode
    int in_range=0;
    for (int i=0;i<5;i++) {
      int pc=get_pc();
      if (pc>=0xe1af&&pc<=0xe1b4) in_range++; else printf("Odd PC=$%04x\n",pc);
    }
    if (in_range>3) {
      // We are in C65 BASIC main loop, so assume it is C65 mode
      saw_c65_mode=1;
      printf("CPU in C65 BASIC 10 main loop.\n");
      return 0;
    }
  } else if (mem_buff[0]==0x00) {
    // Probably C64 mode
    int in_range=0;
    for (int i=0;i<5;i++) {
      int pc=get_pc();
      // XXX Might not work with OpenROMs?
      if (pc>=0xe5cd&&pc<=0xe5d5) in_range++;  else printf("Odd PC=$%04x\n",pc);
    }
    if (in_range>3) {
      // We are in C64 BASIC main loop, so assume it is C65 mode
      saw_c64_mode=1;
      printf("CPU in C64 BASIC 2 main loop.\n");
      return 0;
    }
  }
  printf("Could not determine C64/C65/MEGA65 mode.\n");
  return 1;
}

unsigned char vic_regs[0x400];
#define MAX_SCREEN_SIZE (128*1024)
unsigned char screen_data[MAX_SCREEN_SIZE];
unsigned char colour_data[MAX_SCREEN_SIZE];
unsigned char char_data[8192*8];

unsigned char mega65_rgb(int colour,int rgb)
{
  return
    ((vic_regs[0x0100+(0x100*rgb)+colour]&0xf)<<4)+
    ((vic_regs[0x0100+(0x100*rgb)+colour]&0xf0)>>4);
}

png_bytep *png_rows[576];
int is_pal_mode=0;

int set_pixel(int x,int y,int r,int g, int b)
{
  if (y<0||y>(is_pal_mode?575:479)) {
    fprintf(stderr,"ERROR: Impossible y value %d\n",y);
    exit(-1);
  }
  if (x<0||x>719) {
    fprintf(stderr,"ERROR: Impossible x value %d\n",x);
    exit(-1);
  }

  //  printf("Setting pixel at %d,%d to #%02x%02x%02x\n",x,y,b,g,r);
  ((unsigned char *)png_rows[y])[x*3+0]=r;
  ((unsigned char *)png_rows[y])[x*3+1]=g;
  ((unsigned char *)png_rows[y])[x*3+2]=b;
}  

#define SCREEN_POSITION ((800-720)/2)

int do_screen_shot(void)
{
  monitor_sync();
  detect_mode();
  fetch_ram(0xffd3000,0x0400,vic_regs);

  unsigned int screen_address=vic_regs[0x60]+(vic_regs[0x61]<<8)+(vic_regs[0x62]<<16);
  unsigned int charset_address=vic_regs[0x68]+(vic_regs[0x69]<<8)+(vic_regs[0x6A]<<16);
  if (charset_address==0x1000) charset_address=0x2D000;
  if (charset_address==0x9000) charset_address=0x3D000;
  if (charset_address==0x1800) charset_address=0x2D800;
  if (charset_address==0x9800) charset_address=0x3D800;

  is_pal_mode=(vic_regs[0x6f]&0x80)^0x80;
  unsigned int screen_line_step=vic_regs[0x58]+(vic_regs[0x59]<<8);
  unsigned int colour_address=vic_regs[0x64]+(vic_regs[0x65]<<8);
  unsigned int screen_width=vic_regs[0x5e];
  unsigned int upper_case=2-(vic_regs[0x18]&2);
  unsigned int screen_rows=1+vic_regs[0x7B];
  unsigned int sixteenbit_mode=vic_regs[0x54]&1;
  unsigned int screen_size=screen_line_step*screen_rows*(1+sixteenbit_mode);
  unsigned int charset_size=2048;
  unsigned int extended_background_mode=vic_regs[0x11]&0x40;
  unsigned int multicolour_mode=vic_regs[0x16]&0x10;
  int bitmap_mode=vic_regs[0x11]&0x20;
  
  int border_colour=vic_regs[0x20];
  int background_colour=vic_regs[0x21];

  unsigned int y_scale=vic_regs[0x5B];
  unsigned int h640=vic_regs[0x31]&0x80;
  unsigned int v400=vic_regs[0x31]&0x08;
  unsigned int viciii_attribs=vic_regs[0x31]&0x20;
  unsigned int chargen_x=(vic_regs[0x4c]+(vic_regs[0x4d]<<8))&0xfff;
  chargen_x; // Also measured in pixelclock ticks
  chargen_x-=SCREEN_POSITION; // adjust for pipeline delay
  unsigned int chargen_y=(vic_regs[0x4e]+(vic_regs[0x4f]<<8))&0xfff;  
    
  unsigned int top_border_y=(vic_regs[0x48]+(vic_regs[0x49]<<8))&0xfff;
  unsigned int bottom_border_y=(vic_regs[0x4A]+(vic_regs[0x4B]<<8))&0xfff;
  // side border width is measured in pixelclock ticks, so divide by 3
  unsigned int side_border_width=((vic_regs[0x5C]+(vic_regs[0x5D]<<8))&0xfff);
  unsigned int left_border=side_border_width-SCREEN_POSITION; // Adjust for screen position
  unsigned int right_border=800-side_border_width-SCREEN_POSITION;
  unsigned int x_scale_120=vic_regs[0x5A];
  // x_scale is actually in 120ths of a pixel.
  // so 120 = 1 pixel wide
  // 60 = 2 pixels wide
  float x_step=x_scale_120/120.0;
  if (!h640) x_step/=2;
  printf("x_scale_120=$%02x\n",x_scale_120);
  
  // Check if we are in 16-bit text mode, without full-colour chars for char IDs > 255
  if (sixteenbit_mode&&(!(vic_regs[0x54]&4))) {
    charset_size=8192*8;
  }
  
  if (screen_size>MAX_SCREEN_SIZE) {
    fprintf(stderr,"ERROR: Implausibly large screen size of %d bytes: %d rows, %d columns\n",
	    screen_line_step,screen_rows);
    exit(-1);
  }

  fprintf(stderr,"Screen is at $%07x, width= %d chars, height= %d rows, size=%d bytes, uppercase=%d, line_step= %d\n",
	  screen_address,screen_width,screen_rows,screen_size,upper_case,screen_line_step);
  
  fprintf(stderr,"Fetching screen data...\n");
  fetch_ram(screen_address,screen_size,screen_data);
  fprintf(stderr,"Fetching colour data...\n");
  fetch_ram(0xff80000+colour_address,screen_size,colour_data);

  fprintf(stderr,"Fetching charset...\n");
  fetch_ram(charset_address,charset_size,char_data);
  
  fprintf(stderr,"Have all data.\n");

  //  dump_bytes(0,"screen data",screen_data,screen_size);

  // printf("Got screen line @ $%x. %d to go.\n",screen_address,screen_rows_remaining);

#ifndef WINDOWS
  // Display a thin border
  printf("%c[48;2;%d;%d;%dm",
	 27,
	 ((vic_regs[0x0100+border_colour]&0xf)<<4)+
	 ((vic_regs[0x0100+border_colour]&0xf0)>>4),
	 ((vic_regs[0x0200+border_colour]&0xf)<<4)+
	 ((vic_regs[0x0200+border_colour]&0xf0)>>4),
	 ((vic_regs[0x0300+border_colour]&0xf)<<4)+
	 ((vic_regs[0x0300+border_colour]&0xf0)>>4));
  for(int x=0;x<(1+screen_width+1);x++) printf(" ");
  printf("%c[0m\n",27);
  
  
  for(int y=0;y<screen_rows;y++) {
    //    dump_bytes(0,"row data",&screen_data[y*screen_line_step],screen_width*(1+sixteenbit_mode));

    printf("%c[48;2;%d;%d;%dm ",
	   27,
	   ((vic_regs[0x0100+border_colour]&0xf)<<4)+
	   ((vic_regs[0x0100+border_colour]&0xf0)>>4),
	   ((vic_regs[0x0200+border_colour]&0xf)<<4)+
	   ((vic_regs[0x0200+border_colour]&0xf0)>>4),
	   ((vic_regs[0x0300+border_colour]&0xf)<<4)+
	   ((vic_regs[0x0300+border_colour]&0xf0)>>4));
    
    for(int x=0;x<screen_width;x++) {

      // XXX Support VIC-II extended background colour mode
      // XXX Support VIC-III extended attributes
      // XXX Support VIC-IV 16-bit character mode attributes and all the rest
      // XXX Support VIC-IV full-colour text
      int char_background_colour;
      int char_id=0;
      int char_value=screen_data[y*screen_line_step+x*(1+sixteenbit_mode)];
      if (sixteenbit_mode)
	char_value|=(screen_data[y*screen_line_step+x*(1+sixteenbit_mode)+1]<<8);
      int colour_value=colour_data[y*screen_line_step+x*(1+sixteenbit_mode)];
      if (sixteenbit_mode)
	colour_value|=(colour_data[y*screen_line_step+x*(1+sixteenbit_mode)+1]<<8);
      if (extended_background_mode) {
	char_id=char_value&=0x3f;
	char_background_colour=vic_regs[0x21+((char_value>>6)&3)];
      } 
      else {
	char_id=char_value&0x1fff;
	char_background_colour=background_colour;
      }
      int glyph_width_deduct=char_value>>13;

      // Set foreground and background colours
      int foreground_colour=colour_value&0xff;
      int glyph_flip_vertical=colour_value&0x8000;     
      int glyph_flip_horizontal=colour_value&0x4000;      
      int glyph_with_alpha=colour_value&0x2000;     
      int glyph_goto=colour_value&0x1000;
      int glyph_full_colour=0;
      int glyph_blink=0;
      int glyph_underline=0;
      int glyph_bold=0;
      int glyph_reverse=0;
      if (viciii_attribs&&(!multicolour_mode)) {
	glyph_blink=colour_value&0x0010;
	glyph_reverse=colour_value&0x0020;
	glyph_bold=colour_value&0x0040;
	glyph_underline=colour_value&0x0080;
	if (glyph_bold) foreground_colour|=0x10;
      }
      if (vic_regs[0x54]&2) if (char_id<0x100) glyph_full_colour=1;
      if (vic_regs[0x54]&4) if (char_id>0x0FF) glyph_full_colour=1;
      int glyph_4bit=colour_value&0x0800;
      if (glyph_4bit) glyph_full_colour=1;
      if (colour_value&0x0400) glyph_width_deduct+=8;

      int fg=foreground_colour;
      int bg=char_background_colour;
      if (glyph_reverse) {
	bg=foreground_colour;
	fg=char_background_colour;
      }
      printf("%c[48;2;%d;%d;%dm%c[38;2;%d;%d;%dm",
	     27,
	     ((vic_regs[0x0100+bg]&0xf)<<4)+
	     ((vic_regs[0x0100+bg]&0xf0)>>4),
	     ((vic_regs[0x0200+bg]&0xf)<<4)+
	     ((vic_regs[0x0200+bg]&0xf0)>>4),
	     ((vic_regs[0x0300+bg]&0xf)<<4)+
	     ((vic_regs[0x0300+bg]&0xf0)>>4),
	     27,
	     ((vic_regs[0x0100+fg]&0xf)<<4)+
	     ((vic_regs[0x0100+fg]&0xf0)>>4),
	     ((vic_regs[0x0200+fg]&0xf)<<4)+
	     ((vic_regs[0x0200+fg]&0xf0)>>4),
	     ((vic_regs[0x0300+fg]&0xf)<<4)+
	     ((vic_regs[0x0300+fg]&0xf0)>>4)
	     );

      // Xterm can't display arbitrary graphics, so just mark full-colour chars
      if (glyph_full_colour) {
	printf("?");
	if (glyph_4bit) printf("?");
      }
      else print_screencode(char_id&0xff,upper_case);
    }

    printf("%c[48;2;%d;%d;%dm ",
	   27,
	   ((vic_regs[0x0100+border_colour]&0xf)<<4)+
	   ((vic_regs[0x0100+border_colour]&0xf0)>>4),
	   ((vic_regs[0x0200+border_colour]&0xf)<<4)+
	   ((vic_regs[0x0200+border_colour]&0xf0)>>4),
	   ((vic_regs[0x0300+border_colour]&0xf)<<4)+
	   ((vic_regs[0x0300+border_colour]&0xf0)>>4));
    
    // Set foreground and background colours back to normal at end of each line, before newline    
    printf("%c[0m\n",27);
  }
  printf("%c[48;2;%d;%d;%dm",
	 27,
	 ((vic_regs[0x0100+border_colour]&0xf)<<4)+
	 ((vic_regs[0x0100+border_colour]&0xf0)>>4),
	 ((vic_regs[0x0200+border_colour]&0xf)<<4)+
	 ((vic_regs[0x0200+border_colour]&0xf0)>>4),
	 ((vic_regs[0x0300+border_colour]&0xf)<<4)+
	 ((vic_regs[0x0300+border_colour]&0xf0)>>4));
  for(int x=0;x<(1+screen_width+1);x++) printf(" ");
  printf("%c[0m",27);

#endif
  
  printf("\n");

  printf("Rendering pixel-exact version to mega65-screen.png...\n");

  png_bytep row = NULL;
  FILE *f=NULL;
  char filename[1024];
  for(int n=0;n<1000000;n++)
    {
      snprintf(filename,1024,"mega65-screen-%06d.png",n);
      f = fopen(filename, "rb");
      if (!f) break;
    }
  f = fopen(filename, "wb");
  if (!f) {
    fprintf(stderr,"ERROR: Could not open mega65-screen.png for writing.\n");
    return -1;
  }
  printf("Writing to %s\n",filename);
  
  png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png_ptr) {
    fprintf(stderr,"ERROR: Could not creat PNG structure.\n");
    return -1;
  }

  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr) {
    fprintf(stderr,"ERROR: Could not creat PNG info structure.\n");
    return -1;
  }

  png_init_io(png_ptr, f);

  // Set image size based on PAL or NTSC video mode
  png_set_IHDR(png_ptr, info_ptr, 720, is_pal_mode? 576 : 480,
	       8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
	       PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

  png_write_info(png_ptr, info_ptr);

  // Allocate frame buffer for image, and set all pixels to the border colour by default
  printf("Allocating PNG frame buffer...\n");
  for(int y=0;y<(is_pal_mode?576:480);y++) {
    png_rows[y]=(png_bytep) malloc(3 * 720 * sizeof(png_byte));
    if (!png_rows[y]) {
      perror("malloc()");
      return -1;
    }
    // Set all pixels to border colour
    for(int x=0;x<720;x++) {
      ((unsigned char *)png_rows[y])[x*3+0]=mega65_rgb(border_colour,0);
      ((unsigned char *)png_rows[y])[x*3+1]=mega65_rgb(border_colour,1);
      ((unsigned char *)png_rows[y])[x*3+2]=mega65_rgb(border_colour,2);
    }
  }

  printf("Rendering screen...\n");

  // Start by drawing the non-border area
  for(int y=top_border_y;y<bottom_border_y&&(y<(is_pal_mode?576:480));y++)
    {
      for(int x=left_border;x<right_border;x++) {
	((unsigned char *)png_rows[y])[x*3+0]=mega65_rgb(background_colour,0);
	((unsigned char *)png_rows[y])[x*3+1]=mega65_rgb(background_colour,1);
	((unsigned char *)png_rows[y])[x*3+2]=mega65_rgb(background_colour,2);
      }
    }

  unsigned char bitmap_multi_colour;
  
  // Now render the text display
  int y_position=chargen_y;
  for(int cy=0;cy<screen_rows;cy++) {
    if (y_position>=(is_pal_mode?576:480)) break;
    
    int x_position=chargen_x;

    int xc=0;     
    
    for(int cx=0;cx<screen_width;cx++) {

      //      printf("Rendering char (%d,%d) at (%d,%d)\n",cx,cy,x_position,y_position);
      int char_background_colour;
      int char_id=0;
      int char_value=screen_data[cy*screen_line_step+cx*(1+sixteenbit_mode)];
      if (sixteenbit_mode)
	char_value|=(screen_data[cy*screen_line_step+cx*(1+sixteenbit_mode)+1]<<8);
      int colour_value=colour_data[cy*screen_line_step+cx*(1+sixteenbit_mode)];
      if (sixteenbit_mode)
	colour_value|=(colour_data[cy*screen_line_step+cx*(1+sixteenbit_mode)+1]<<8);
      if (extended_background_mode) {
	char_id=char_value&=0x3f;
	char_background_colour=vic_regs[0x21+((char_value>>6)&3)];
      } 
      else {
	char_id=char_value&0x1fff;
	char_background_colour=background_colour;
      }
      int glyph_width_deduct=char_value>>13;
      
      // Set foreground and background colours
      int foreground_colour=colour_value&0x0f;
      int glyph_flip_vertical=colour_value&0x8000;     
      int glyph_flip_horizontal=colour_value&0x4000;      
      int glyph_with_alpha=colour_value&0x2000;     
      int glyph_goto=colour_value&0x1000;
      int glyph_full_colour=0;
      int glyph_blink=0;
      int glyph_underline=0;
      int glyph_bold=0;
      int glyph_reverse=0;
      if (viciii_attribs&&(!multicolour_mode)) {
	glyph_blink=colour_value&0x0010;
	glyph_reverse=colour_value&0x0020;
	glyph_bold=colour_value&0x0040;
	glyph_underline=colour_value&0x0080;
	if (glyph_bold) foreground_colour|=0x10;
      }
      if (multicolour_mode) foreground_colour=colour_value&0xff;      

      if (bitmap_mode) {
	char_value=screen_data[cy*screen_line_step+cx*(1+sixteenbit_mode)];
	foreground_colour=char_value&0xf;
	background_colour=char_value>>4;
	bitmap_multi_colour=colour_data[cy*screen_line_step+cx*(1+sixteenbit_mode)];
	if (0) printf("Bitmap fore/background colours are $%x / $%x\n",foreground_colour,background_colour);
      }     
      
      if (vic_regs[0x54]&2) if (char_id<0x100) glyph_full_colour=1;
      if (vic_regs[0x54]&4) if (char_id>0x0FF) glyph_full_colour=1;
      int glyph_4bit=colour_value&0x0800;
      if (glyph_4bit) glyph_full_colour=1;
      if (colour_value&0x0400) glyph_width_deduct+=8;

      // Lookup the char data, and work out how many pixels we need to paint
      int glyph_width=8;
      if (glyph_4bit) glyph_width=16; 
      glyph_width-=glyph_width_deduct;

      // For each row of the glyph
      for(int yy=0;yy<8;yy++) {
	int glyph_row=yy;
	if (glyph_flip_vertical) glyph_row=7-glyph_row;

	unsigned char glyph_data[8];       
	
	if (glyph_full_colour) {
	  // Get 8 bytes of data
	  fetch_ram_cacheable(char_id*64+glyph_row*8,8,glyph_data);
	} else {
	  // Use existing char data we have already fetched
	  //	  printf("Chardata for char $%03x = $%02x\n",char_id,char_data[char_id*8+glyph_row]);
	  if (!bitmap_mode) {
	    for(int i=0;i<8;i++)
	      if ((char_data[char_id*8+glyph_row]>>i)&1) glyph_data[i]=0xff; else glyph_data[i]=0;
	  } else {
	    int addr=charset_address&0xfe000;
	    addr+=cx*8+cy*320+glyph_row;
	    if (h640) {
	      addr=charset_address&0xfc000;
	      addr+=cx*8+cy*640+glyph_row;
	    }
	    unsigned char pixels;
	    fetch_ram_cacheable(addr,1,&pixels);
	    if (0) printf("Reading bitmap data from $%x = $%02x, charset_address=$%x\n",
			  addr,pixels,charset_address);
	    for(int i=0;i<8;i++)
	      if ((pixels>>i)&1) glyph_data[i]=0xff; else glyph_data[i]=0;
	    
	  }
	}
	
	
	if (glyph_flip_horizontal) {
	  unsigned char b[8];
	  for(int i=0;i<8;i++) b[i]=glyph_data[i];
	  for(int i=0;i<8;i++) glyph_data[i]=b[7-i];
	}

	if (glyph_reverse) {
	  for(int i=0;i<8;i++) glyph_data[i]=0xff-glyph_data[i];	  
	}

	// XXX Do blink with PNG animation?

	if (glyph_underline&&(yy==7)) {
	  for(int i=0;i<8;i++) glyph_data[i]=0xff;
	}

	xc=0;
	for(float xx=0;xx<glyph_width;xx+=x_step) {
	  int r=mega65_rgb(background_colour,0);
	  int g=mega65_rgb(background_colour,1);
	  int b=mega65_rgb(background_colour,2);

	  if (glyph_4bit) {
	    // 16-colour 4 bits per pixel
	    int c=glyph_data[((int)xx)/2];
	    if (((int)xx)&1) c=c>>4; else c=c&0xf;
	    if (glyph_with_alpha) {
	      // Alpha blended pixels:
	      // Here we blend the foreground and background colours we already know
	      // according to the alpha value
	      int a=c;
	      r=(mega65_rgb(foreground_colour,0)*a + mega65_rgb(background_colour,0)*(15 -a))>>8;
	      g=(mega65_rgb(foreground_colour,1)*a + mega65_rgb(background_colour,1)*(15 -a))>>8;
	      b=(mega65_rgb(foreground_colour,2)*a + mega65_rgb(background_colour,2)*(15 -a))>>8;
	    } else {
	      r=mega65_rgb(c,0);
	      g=mega65_rgb(c,1);
	      b=mega65_rgb(c,2);
	    }
	    
	  } else if (glyph_full_colour) {
	    // 256-colour 8 bits per pixel
	    if (glyph_with_alpha) {
	      // Alpha blended pixels:
	      // Here we blend the foreground and background colours we already know
	      // according to the alpha value
	      int a=glyph_data[(int)xx];
	      r=(mega65_rgb(foreground_colour,0)*a + mega65_rgb(background_colour,0)*(255 -a))>>8;
	      g=(mega65_rgb(foreground_colour,1)*a + mega65_rgb(background_colour,1)*(255 -a))>>8;
	      b=(mega65_rgb(foreground_colour,2)*a + mega65_rgb(background_colour,2)*(255 -a))>>8;
	    } else {
	      r=mega65_rgb(glyph_data[(int)xx],0);
	      g=mega65_rgb(glyph_data[(int)xx],1);
	      b=mega65_rgb(glyph_data[(int)xx],2);
	    }
	    
	  } else if (multicolour_mode&&((foreground_colour&8)||bitmap_mode)) {
	    // Multi-colour normal char
	    int bits=0;
	    if (glyph_data[6-(((int)xx)&0x6)]) bits|=1;
	    if (glyph_data[7-(((int)xx)&0x6)]) bits|=2;
	    int colour;
	    if (!bitmap_mode) {
	      switch(bits) {
	      case 0: colour=vic_regs[0x21]; break; // background colour
	      case 1: colour=vic_regs[0x22]; break; // multi colour 1
	      case 2: colour=vic_regs[0x23]; break; // multi colour 2
	      case 3: colour=foreground_colour&7; break; // foreground colour
	      }
	    } else {
	      switch(bits) {
	      case 0: colour=vic_regs[0x21]; break;
	      case 1: colour=background_colour; break; 
	      case 2: colour=foreground_colour; break; 
	      case 3: colour=bitmap_multi_colour&0xf; break;
	      }
	    }
	    r=mega65_rgb(colour,0);
	    g=mega65_rgb(colour,1);
	    b=mega65_rgb(colour,2);

	  } else {
	    // Mono normal char
	    if (glyph_data[7-(int)xx]) {
	      r=mega65_rgb(foreground_colour,0);
	      g=mega65_rgb(foreground_colour,1);
	      b=mega65_rgb(foreground_colour,2);
	      //	      printf("Foreground pixel. colour = $%02x = #%02x%02x%02x\n",
	      //		     foreground_colour,b,g,r);
	    }
	  }
	  
	  // Actually draw the pixels
	  for(int yc=0;yc<=y_scale;yc++) {
	    if (((y_position+yc)<bottom_border_y)
		&&((y_position+yc)>=top_border_y)
		&& ((x_position+xc)<right_border)
		&& ((x_position+xc)>=left_border)
		)
	      set_pixel(x_position+xc,y_position+yc+yy*(1+y_scale),r,g,b);
	  }
	  xc++;
	}
      }
      
      // Advance for width of the glyph
      //      printf("Char was %d pixels wide.\n",xc);
      x_position+=xc;
    }
    y_position+=8*(1+y_scale);
  }
  
  printf("Writing out PNG frame buffer...\n");
  // Write out each row of the PNG
  for(int y=0;y<(is_pal_mode?576:480);y++)
    png_write_row(png_ptr, png_rows[y]);

  png_write_end(png_ptr, NULL);

  fclose(f);
  printf("\n");
  
  return 0;
}


int process_line(char *line,int live)
{
  int pc,a,x,y,sp,p;
  //  printf("[%s]\n",line);
  if (!live) return 0;
  if (strstr(line,"ws h RECA8LHC")) {
    if (!new_monitor) printf("Detected new-style UART monitor.\n");
    new_monitor=1;
  }
  if (sscanf(line,"%04x %02x %02x %02x %02x %02x",
	     &pc,&a,&x,&y,&sp,&p)==6) {
    printf("PC=$%04x\n",pc);
    if (pc==0xf4a5||pc==0xf4a2||pc==0xf666) {
      // Intercepted LOAD command
      printf("LOAD vector intercepted\n");
      state=1;
    } else if ( //  (pc>=0x8000&&pc<0xc000)&&
	       (hyppo)) {
      int patchKS=0;
      if (romfile&&(!flashmenufile)) patchKS=1;
#ifdef WINDOWS
      fprintf(stderr,"[T+%I64dsec] Replacing %shyppo...\n",
	      (long long)time(0)-start_time,
	      patchKS?"and patching ":"");
#else
      fprintf(stderr,"[T+%lldsec] Replacing %shyppo...\n",
	      (long long)time(0)-start_time,
	      patchKS?"and patching ":"");
#endif
      stop_cpu();
      if (hyppo) { load_file(hyppo,0xfff8000,patchKS); } hyppo=NULL;
      if (romfile) { load_file(romfile,0x20000,0); } romfile=NULL;
      if (flashmenufile) { load_file(flashmenufile,0x50000,0); } romfile=NULL;
      if (charromfile) load_file(charromfile,0xFF7E000,0);
      if (colourramfile) load_file(colourramfile,0xFF80000,0);
      if (virtual_f011) {
	char cmd[1024];
#ifdef WINDOWS
	fprintf(stderr,"[T+%I64dsec] Virtualising F011 FDC access.\n",
		(long long)time(0)-start_time);
#else
	fprintf(stderr,"[T+%lldsec] Virtualising F011 FDC access.\n",
		(long long)time(0)-start_time);
#endif
	// Enable FDC virtualisation
	snprintf(cmd,1024,"sffd3659 01\r");
	slow_write(fd,cmd,strlen(cmd));
	usleep(20000);
	// Enable disk 0 (including for write)
	snprintf(cmd,1024,"sffd368b 03\r");
	slow_write(fd,cmd,strlen(cmd));
      }
      charromfile=NULL;
      colourramfile=NULL;
      if (!virtual_f011) restart_hyppo();
      else {
	hypervisor_paused=1;
	printf("hypervisor paused\n");
      }
    } else {
      if (state==99) {
	// Synchronised with monitor
	state=0;
	// Send ^U r <return> to print registers and get into a known state.
	usleep(50000);
	slow_write(fd,"\r",1);
	if (!halt) {
	  start_cpu();
	}
	usleep(20000);
	if (reset_first) { slow_write(fd,"!\r",2); sleep(1); }
	if (pal_mode) { slow_write(fd,"sffd306f 0\r",12); usleep(20000); }
	if (ntsc_mode) { slow_write(fd,"sffd306f 80\r",12); usleep(20000); }
	if (ethernet_video) {
	  slow_write(fd,"sffd36e1 29\r",12); // turn on video streaming over ethernet
	  usleep(20000);
	}
	if (ethernet_cpulog) {
	  slow_write(fd,"sffd36e1 05\r",12); // turn on cpu instruction log streaming over ethernet
	  usleep(20000);
	}
	printf("Synchronised with monitor.\n");

	if (zap) {
	  char cmd[1024];
	  usleep(20000);
	  snprintf(cmd,1024,"sffd36c8 %x %x %x %x\r",
		   (zap_addr>>0)&0xff,
		   (zap_addr>>8)&0xff,
		   (zap_addr>>16)&0xff,
		   (zap_addr>>24)&0xff);
	  slow_write(fd,cmd,strlen(cmd));	  
	  usleep(20000);
	  snprintf(cmd,1024,"sffd36cf 42\r");
	  slow_write(fd,cmd,strlen(cmd));
	  fprintf(stderr,"FPGA reconfigure command issued.\n");
	}
	
	if (break_point!=-1) {
	  fprintf(stderr,"Setting CPU breakpoint at $%04x\n",break_point);
	  char cmd[1024];
	  sprintf(cmd,"b%x\r",break_point);
	  usleep(20000);
	  slow_write(fd,cmd,strlen(cmd));
	  do_exit(0);
	}
	
	if (type_text) {
	  fprintf(stderr,"Typing text via virtual keyboard...\n");
	  {
	    int i;
	    for(i=0;type_text[i];i++) {
	      int c1=0x7f;
	      int c2=0x7f;
	      int c=tolower(type_text[i]);
	      if (c!=type_text[i]) c2=0x0f; // left shift for upper case letters
	      // Punctuation that requires shifts
	      switch (c)
		{
		case '!': c='1'; c2=0x0f; break;
		case '\"': c='2'; c2=0x0f; break;
		case '#': c='3'; c2=0x0f; break;
		case '$': c='4'; c2=0x0f; break;
		case '%': c='5'; c2=0x0f; break;
		case '(': c='8'; c2=0x0f; break;
		case ')': c='9'; c2=0x0f; break;
		case '?': c='/'; c2=0x0f; break;
		case '<': c=','; c2=0x0f; break;
		case '>': c='.'; c2=0x0f; break;
		}
	      switch (c)
		{
		case '~':
		  // control sequences
		  switch (type_text[i+1])
		    {
		    case 'C': c1=0x3f; break;              // RUN/STOP
		    case 'D': c1=0x07; break;              // down
		    case 'U': c1=0x07; c2=0x0f; break;     // up
		    case 'L': c1=0x02; break;              // left
		    case 'H': c1=0x33; break;              // HOME
		    case 'R': c1=0x02; c2=0x0f; break;     // right
		    case 'M': c1=0x01; break;              // RETURN 
		    case 'T': c1=0x00; break;              // INST/DEL
		    case '1': c1=0x04; break; // F1
		    case '3': c1=0x05; break; // F3
		    case '5': c1=0x06; break; // F5
		    case '7': c1=0x03; break; // F7
		    }
		  i++;
		  break;
		case '3': c1=0x08; break;
		case 'w': c1=0x09; break;
		case 'a': c1=0x0a; break;
		case '4': c1=0x0b; break;
		case 'z': c1=0x0c; break;
		case 's': c1=0x0d; break;
		case 'e': c1=0x0e; break;

		case '5': c1=0x10; break;
		case 'r': c1=0x11; break;
		case 'd': c1=0x12; break;
		case '6': c1=0x13; break;
		case 'c': c1=0x14; break;
		case 'f': c1=0x15; break;
		case 't': c1=0x16; break;
		case 'x': c1=0x17; break;

		case '7': c1=0x18; break;
		case 'y': c1=0x19; break;
		case 'g': c1=0x1a; break;
		case '8': c1=0x1b; break;
		case 'b': c1=0x1c; break;
		case 'h': c1=0x1d; break;
		case 'u': c1=0x1e; break;
		case 'v': c1=0x1f; break;

		case '9': c1=0x20; break;
		case 'i': c1=0x21; break;
		case 'j': c1=0x22; break;
		case '0': c1=0x23; break;
		case 'm': c1=0x24; break;
		case 'k': c1=0x25; break;
		case 'o': c1=0x26; break;
		case 'n': c1=0x27; break;

		case '+': c1=0x28; break;
		case 'p': c1=0x29; break;
		case 'l': c1=0x2a; break;
		case '-': c1=0x2b; break;
		case '.': c1=0x2c; break;
		case ':': c1=0x2d; break;
		case '@': c1=0x2e; break;
		case ',': c1=0x2f; break;

		case '}': c1=0x30; break;  // British pound symbol
		case '*': c1=0x31; break;
		case ';': c1=0x32; break;
		case 0x13: c1=0x33; break; // home
		  // case '': c1=0x34; break; right shift
		case '=': c1=0x35; break;
		case 0x91: c1=0x36; break;
		case '/': c1=0x37; break;

		case '1': c1=0x38; break;
		case '_': c1=0x39; break;
		  // case '': c1=0x3a; break; control
		case '2': c1=0x3b; break;
		case ' ': c1=0x3c; break;
		  // case '': c1=0x3d; break; C=
		case 'q': c1=0x3e; break;
		case 0x0c: c1=0x3f; break;

		default: c1=0x7f;
		}
	      char cmd[1024];
	      snprintf(cmd,1024,"sffd3615 %02x %02x\n",c1,c2);
	      slow_write(fd,cmd,strlen(cmd));
	      // Stop pressing keys
	      slow_write(fd,"sffd3615 7f 7f 7f \n",19);
	    }
	    // RETURN at end if requested
	    if (type_text_cr)
	      slow_write(fd,"sffd3615 01 7f 7f \n",19);
	    // Stop pressing keys
	    slow_write(fd,"sffd3615 7f 7f 7f \n",19);
	    // Typing mode does only typing
	    do_exit(0);
	  }
	}
      }
    }
  }
  if (sscanf(line," :00000B7 %02x %*02x %*02x %*02x %02x %02x",
	     &name_len,&name_lo,&name_hi)==3) {
    if (not_already_loaded||name_len>1) {
      name_addr=(name_hi<<8)+name_lo;
      printf("Filename is %d bytes long, and is stored at $%04x\n",
	     name_len,name_addr);
      char filename[16];
      snprintf(filename,16,"m%04x\r",name_addr);
      usleep(10000);
      slow_write(fd,filename,strlen(filename));
      printf("Asking for filename from memory: %s\n",filename);
      state=3;
    }
  }
  if (sscanf(line,":000000B7:%08x%08x",
	     &name_len,&name_addr)==2) {
    if (not_already_loaded) {
      name_len=name_len>>24;
      printf("Filename is %d bytes long, from 0x%08x\n",
	     name_len,name_addr);
      name_addr=(name_addr>>24)+((name_addr>>8)&0xff00);
      printf("Filename is %d bytes long, and is stored at $%04x\n",
	     name_len,name_addr);
      char filename[16];
      snprintf(filename,16,"m%04x\r",name_addr);
      usleep(10000);
      slow_write(fd,filename,strlen(filename));
      printf("Asking for filename from memory: %s\n",filename);
      state=3;
    }
  }
  {
    int addr;
    int b[16];
    int gotIt=0;
    unsigned int v[4];
    if (line[0]=='?') fprintf(stderr,"%s\n",line);
    if (sscanf(line,":%x:%08x%08x%08x%08x",
	       &addr,&v[0],&v[1],&v[2],&v[3])==5) {
      for(int i=0;i<16;i++) b[i]=(v[i/4]>>( (3-(i&3))*8)) &0xff;
      gotIt=1;
    }
    if (sscanf(line," :%x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
	       &addr,
	       &b[0],&b[1],&b[2],&b[3],
	       &b[4],&b[5],&b[6],&b[7],
	       &b[8],&b[9],&b[10],&b[11],
	       &b[12],&b[13],&b[14],&b[15])==17) gotIt=1;
    if (gotIt) {
      char fname[17];
      //      if (!screen_shot) printf("Read memory @ $%04x\n",addr);
      if (addr>=0xfffbc00&&addr<0xfffc000) {
	for(int i=0;i<16;i++) hyppo_buffer[addr-0xfffbc00+i]=b[i];
	if (addr==0xfffbff0) {
	  show_hyppo_report();
	  exit(0);
	}
      }
      if (addr==name_addr) {
	for(int i=0;i<16;i++) { fname[i]=b[i]; } fname[16]=0;
	fname[name_len]=0;
	printf("Request to load '%s'\n",fname);
	if (fname[0]=='!'||(!strcmp(fname,"0:!"))) {
	  // we use this form in case junk gets typed while we are doing it
	  if (not_already_loaded)
	    state=2; // load specified file
	  not_already_loaded=0;
	  // and change filename, so that we don't get stuck in a loop repeatedly loading
	  char cmd[1024];
	  snprintf(cmd,1024,"s%x 41\r",name_addr);
	  fprintf(stderr,"Replacing filename: %s\n",cmd);
	  slow_write(fd,cmd,strlen(cmd));
	}
	else {
	  printf("Specific file to load is '%s'\n",fname);
	  if (filename) free(filename);
	  filename=strdup(fname);
	  do_go64=1; // load in C64 mode only
	  state=0;
	}
      }
      else if(addr == sdbuf_request_addr) {
	printf("Saw data for write buffer @ $%x\n",addr);
       
	int i;
	for(i=0;i<16;i++)
	  sd_sector_buf[sdbuf_request_addr-WRITE_SECTOR_BUFFER_ADDRESS+i]=b[i];
        sdbuf_request_addr += 16;

        if(sdbuf_request_addr == (WRITE_SECTOR_BUFFER_ADDRESS+0x100)) {
	  // Request next $100 of buffer
	  char cmd[1024];
	  sprintf(cmd,"M%x\r",sdbuf_request_addr);
	  printf("Requesting reading of second half of sector buffer: %s",cmd);
	  slow_write(fd,cmd,strlen(cmd));
	}
	
        if(sdbuf_request_addr == (WRITE_SECTOR_BUFFER_ADDRESS+0x200)) {

	  dump_bytes(0,"Sector to write",sd_sector_buf,512);
	  
          char cmd[1024];

	  int physical_sector=( saved_side==0 ? saved_sector-1 : saved_sector+9 );
	  int result = fseek(fd81, (saved_track*20+physical_sector)*512, SEEK_SET);
	  if(result) {

	    fprintf(stderr, "Error finding D81 sector %d %d\n", result, (saved_track*20+physical_sector)*512);
	    exit(-2);
	  }
	  else {
            int b=-1;
	    b=fwrite(sd_sector_buf,1,512,fd81);
            if(b!=512) {

	      fprintf(stderr, "Could not write D81 file: '%s'\n", d81file);
	      exit(-1);
            }
	    fprintf(stderr, "write: %d @ 0x%x\n", b, (saved_track*20+physical_sector)*512);
          }

          // block loaded save it now
          sdbuf_request_addr = 0;

          snprintf(cmd,1024,"sffd3086 %02x\n",saved_side);
	  slow_write(fd,cmd,strlen(cmd));
          if (!halt) start_cpu();
        }
      }
      if (addr==0xffd3659) {
	fprintf(stderr,"Hypervisor virtualisation flags = $%02x\n",b[0]);
	if (virtual_f011&&hypervisor_paused) restart_hyppo();
	hypervisor_paused=0;
        printf("hyperv not paused\n");
      }
    }
  }
  if ((!strcmp(line," :0000800 A0 A0 A0 A0 A0 A0 A0 A0 A0 A0 A0 A0 A0 A0 A0 A0"))
      ||(!strcmp(line,":00000800:A0A0A0A0A0A0A0A0A0A0A0A0A0A0A0A0")))
    {

      if (modeline_cmd[0]) {
#ifdef WINDOWS
	fprintf(stderr,"[T+%I64dsec] Setting video modeline\n",(long long)time(0)-start_time);
#else
	fprintf(stderr,"[T+%lldsec] Setting video modeline\n",(long long)time(0)-start_time);
#endif
	fprintf(stderr,"Commands:\n%s\n",modeline_cmd);
	slow_write(fd,modeline_cmd,strlen(modeline_cmd));

	// Disable on-screen keyboard to be sure
	usleep(50000);
	slow_write(fd,"sffd3615 7f\n",12);
      
      
	// Force mode change to take effect, after first giving time for VICIV to recalc parameters      
	usleep(50000);
	slow_write(fd,"sffd3011 1b\n",12);

#if 0
	// Check X smooth-scroll values
	int i;
	for(i=0;i<10;i++)
	  {
	    char cmd[1024];
	    snprintf(cmd,1024,"sffd307d %x\n",i);
	    slow_write(fd,cmd,strlen(cmd));
	    snprintf(cmd,1024,"mffd307d\n");
	    slow_write(fd,cmd,strlen(cmd));
	    usleep(50000);
	    read_and_print(fd);
	  }
#endif
      
	// Then ask for current mode information via VIC-IV registers, but first give a little time
	// for the mode change to take effect
	usleep(100000);
	slow_write(fd,"Mffd3040\n",9);
      
      }
      else if (mode_report) {
	slow_write(fd,"Mffd3040\n",9);
      }

      // We are in C65 mode - switch to C64 mode
      if (osk_enable) {
	char *cmd="sffd3615 ff\r";
	slow_write(fd,cmd,strlen(cmd));      
      }
      if (do_go64) {
	// PGS 20181123 - Keyboard buffer has moved in newer C65 ROMs from $34A to $2D0
	saw_c65_mode=1; stuff_keybuffer("GO64\rY\r");
	saw_c65_mode=0;
#ifdef WINDOWS
	if (first_go64) fprintf(stderr,"[T+%I64dsec] GO64\nY\n",(long long)time(0)-start_time);
#else
	if (first_go64) fprintf(stderr,"[T+%lldsec] GO64\nY\n",(long long)time(0)-start_time);
#endif
	first_go64=0;
      } else {
	if (!saw_c65_mode) fprintf(stderr,"MEGA65 is in C65 mode.\n");
	saw_c65_mode=1;
	if ((!do_go64)&&filename&&not_already_loaded) {
	  printf("XXX Trying to load from C65 mode\n");
	  char *cmd;
	  cmd="bf664\r";
	  slow_write(fd,cmd,strlen(cmd));
	  stuff_keybuffer("DLo\"!\r");
#ifdef WINDOWS
	  if (first_load) fprintf(stderr,"[T+%I64dsec] Injecting LOAD\"!\n",(long long)time(0)-start_time);
#else
	  if (first_load) fprintf(stderr,"[T+%lldsec] Injecting LOAD\"!\n",(long long)time(0)-start_time);
#endif
	  first_load=0;

	  while(state!=1) {
	    process_waiting(fd);
	  }
	
	} else if ((!mode_report)&&(!virtual_f011)&&(!type_text)) {
	  if (do_run) {
	    // C65 mode stuff keyboard buffer
	    printf("XXX - Do C65 keyboard buffer stuffing\n");
	    //	} else if (screen_shot) {
	    //	  if (!screen_address) {
	    //	    printf("Waiting to capture screen...\n");
	    //	    // We need to get some info about the screen
	    //	    slow_write_safe(fd,"Mffd3058\r",9);
	    //	  }
	  } else {
	    fprintf(stderr,"Exiting now that we are in C65 mode.\n");
	    do_exit(0);
	  }
	}
      }    
    }
  if (// C64 BASIC banner
      (!strcmp(line," :000042C 2A 2A 2A 2A 20 03 0F 0D 0D 0F 04 0F 12 05 20 36"))
      ||(!strcmp(line,":0000042C:2A2A2A2A20030F0D0D0F040F12052036"))
      // MEGA BASIC banner
      ||(!strcmp(line," :000042C 2A 2A 2A 2A 20 0D 05 07 01 36 35 20 0D 05 07 01"))
      ||(!strcmp(line,":0000042C:2A2A2A2A200D0507013635200D050701"))
      ) {
    // C64 mode BASIC -- set LOAD trap, and then issue LOAD command
    char *cmd;
    if (filename&&not_already_loaded) {
      cmd="bf4a5\r";
      saw_c64_mode=1;
      slow_write(fd,cmd,strlen(cmd));
      stuff_keybuffer("Lo\"!\",8,1\r");
#ifdef WINDOWS
      if (first_load) fprintf(stderr,"[T+%I64dsec] LOAD\"!\n",(long long)time(0)-start_time);
#else
      if (first_load) fprintf(stderr,"[T+%lldsec] LOAD\"!\n",(long long)time(0)-start_time);
#endif
      first_load=0;
    } else {
      if (!saw_c64_mode) fprintf(stderr,"MEGA65 is in C64 mode.\n");
      saw_c64_mode=1;
      if( (!virtual_f011)) // &&(!screen_shot))
	do_exit(0);
      //      if (screen_shot) slow_write_safe(fd,"Mffd3058\r",9);
    }
  }  
  if (state==2)
    {
      state=99;
      printf("Filename is %s\n",filename);
      f=fopen(filename,"rb");
      if (f==NULL) {
	fprintf(stderr,"Could not find file '%s'\n",filename);
	exit(-1);
      } else {
	char cmd[1024];
	int load_addr=fgetc(f);
	load_addr|=fgetc(f)<<8;
	if (!comma_eight_comma_one) {
	  if (saw_c64_mode)
	    load_addr=0x0801;
	  else
	    load_addr=0x2001;
	  printf("Forcing load address to $%04X\n",load_addr);
	}
	else
	  printf("Load address is $%04x\n",load_addr);	
	usleep(50000);
	unsigned char buf[16384];
	int max_bytes=4096;
	int b=fread(buf,1,max_bytes,f);
	while(b>0) {
	  printf("Read to $%04x (%d bytes)\n",load_addr,b);
	  fflush(stdout);
	  // load_addr=0x400;
	  if (new_monitor) 
	    sprintf(cmd,"l%x %x\r",load_addr,(load_addr+b)&0xffff);
	  else
	    sprintf(cmd,"l%x %x\r",load_addr-1,load_addr+b-1);
	  slow_write(fd,cmd,strlen(cmd));
	  usleep(1000);
	  int n=b;
	  unsigned char *p=buf;
	  while(n>0) {
	    int w=serialport_write(fd,p,n);
	    if (w>0) { p+=w; n-=w; } else usleep(1000);
	  }
	  if (serial_speed==230400) usleep(10000+50*b);
	  else usleep(10000+6*b);
	  load_addr+=b;
	  b=fread(buf,1,max_bytes,f);	  
	}
	fclose(f); f=NULL;
	// set end address, clear input buffer, release break point,
	// jump to end of load routine, resume CPU at a CLC, RTS
	usleep(50000);

	// Clear keyboard input buffer
	if (saw_c64_mode) sprintf(cmd,"sc6 0\r");
	else sprintf(cmd,"sd0 0\r");
	slow_write(fd,cmd,strlen(cmd));	usleep(20000);

	// Remove breakpoint
	sprintf(cmd,"b\r");
	slow_write(fd,cmd,strlen(cmd));	usleep(20000);

	// We need to set X and Y to load address before
	// returning: LDX #$ll / LDY #$yy / CLC / RTS
	sprintf(cmd,"s380 a2 %x a0 %x 18 60\r",
		load_addr&0xff,(load_addr>>8)&0xff);
	printf("Returning top of load address = $%04X\n",load_addr);
	slow_write(fd,cmd,strlen(cmd));	usleep(20000);

	sprintf(cmd,"g0380\r");
	slow_write(fd,cmd,strlen(cmd));	usleep(20000);

	if (!halt) {
	  start_cpu();
	}

	if (do_run) {
	  stuff_keybuffer("RUN:\r");
#ifdef WINDOWS
	  fprintf(stderr,"[T+%I64dsec] RUN\n",(long long)time(0)-start_time);
#else
	  fprintf(stderr,"[T+%lldsec] RUN\n",(long long)time(0)-start_time);
#endif
	}

	printf("\n");
	// loaded ok.
	printf("LOADED.\n");
	if (!virtual_f011)
	  do_exit(0);
      }
    }
  return 0;
}


char line[1024];
int line_len=0;

int vfdc_track,vfdc_sector,vfdc_side;

int process_char(unsigned char c, int live)
{
  //printf("char $%02x\n",c);

  // Remember recent chars for virtual FDC access, as the Hypervisor tells
  // us which track, sector and side, before it sends the marker
  if (c=='!'&&virtual_f011) {
#ifdef WINDOWS
    printf("[T+%I64dsec] : V-FDC read request from UART monitor: Track:%d, Sector:%d, Side:%d.\n",
	   time(0)-start_time,vfdc_track,vfdc_sector,vfdc_side);
#else
    printf("[T+%ldsec] : V-FDC read request from UART monitor: Track:%d, Sector:%d, Side:%d.\n",
	   time(0)-start_time,vfdc_track,vfdc_sector,vfdc_side);
#endif
    // We have all we need, so just read the sector from disk, upload it, and mark the job done
    virtual_f011_read(0,vfdc_track,vfdc_sector,vfdc_side);
  }
  if (c=='\\'&&virtual_f011) {
#ifdef WINDOWS
    printf("[T+%I64dsec] : V-FDC write request from UART monitor: Track:%d, Sector:%d, Side:%d.\n",
	   time(0)-start_time,vfdc_track,vfdc_sector,vfdc_side);
#else
    printf("[T+%ldsec] : V-FDC write request from UART monitor: Track:%d, Sector:%d, Side:%d.\n",
	   time(0)-start_time,vfdc_track,vfdc_sector,vfdc_side);
#endif
    // We have all we need, so just read the sector from disk, upload it, and mark the job done
    sdbuf_request_addr = WRITE_SECTOR_BUFFER_ADDRESS;
    { char  cmd[1024];
      sprintf(cmd,"M%x\r",sdbuf_request_addr);
      printf("Requesting reading of sector buffer: %s",cmd);
      slow_write(fd,cmd,strlen(cmd));
    }
    saved_side=vfdc_side&0x3f;
    saved_track=vfdc_track;
    saved_sector=vfdc_sector;
    
  }
  vfdc_track=vfdc_sector;
  vfdc_sector=vfdc_side;
  vfdc_side=c&0x7f;

  if (c=='\r'||c=='\n') {
    line[line_len]=0;
    if (line_len>0) process_line(line,live);
    line_len=0;
  } else {
    if (line_len<1023) line[line_len++]=c;
  }
  return 0;
}

int process_waiting(PORT_TYPE fd)
{
  unsigned char  read_buff[1024];
  int b=serialport_read(fd,read_buff,1024);
  while (b>0) {
    int i;
    for(i=0;i<b;i++) {
      process_char(read_buff[i],1);
    }
    b=serialport_read(fd,read_buff,1024);    
  }
  return 0;
}

int assemble_modeline( int *b,
		       int pixel_clock,
		       int hpixels,int hwidth,
		       int vpixels,int vheight,
		       int hsync_polarity,int vsync_polarity,
		       int vsync_start,int vsync_end,
		       int hsync_start_in,int hsync_end_in,
		       int rasters_per_vicii_raster)
{

  // VSYNC pulse ends at end of frame. vsync_delay says how many
  // rasters after vpixels the vsync starts
  // (This means that we need to adjust the start of the frame vertically,
  // for which we don't currently have a register)
  int vsync_rasters=vsync_end-vsync_start+1;
  int vsync_delay=vheight-vpixels-vsync_rasters;

  // Adjust raster length for difference in pixel clock
  float factor=pixel_clock/100000000.0;
  hwidth/=factor;
  if (factor<1) hpixels/=factor;

  if (0) 
    if (hpixels%800) {
      fprintf(stderr,"Adjusting hpixels to %d (modulo was %d)\n",hpixels-hpixels%800,hpixels%800);
      hpixels-=hpixels%800;
    }     
  
  int hsync_start=hsync_start_in+0x10;
  int hsync_end=hsync_end_in+0x10;
  hsync_start/=factor;
  hsync_end/=factor;
  if (hsync_start>=hwidth) hsync_start-=hwidth;
  if (hsync_end>=hwidth) hsync_end=hsync_start + 400;
  if (hsync_end<hsync_start) hsync_end=hsync_start + 400;
  if (hsync_end>=hwidth) hsync_end=hwidth-200;
  fprintf(stderr,"After HSYNC tweak: hsync_start=%d, hsync_end=%d\n",hsync_start,hsync_end);

  int yscale=rasters_per_vicii_raster-1;

  // Primary mode register set
  b[0x72]=/* $D072 */       vsync_delay; 
  b[0x73]=/* $D073 */       ((hsync_end>>10)&0xf)+(yscale<<4);
  b[0x74]=/* $D074 */       (hsync_end>>2)&0xff;
  b[0x75]=/* $D075 */	 (hpixels>>2)&0xff;
  b[0x76]=/* $D076 */	 (hwidth>>2)&0xff;
  b[0x77]=/* $D077 */	 ((hpixels>>10)&0xf) + ((hwidth>>6)&0xf0);
  b[0x78]=/* $D078 */	 vpixels&0xff;
  b[0x79]=/* $D079 */	 vheight&0xff;
  b[0x7a]=/* $D07A */	 ((vpixels>>8)&0xf) + ((vheight>>4)&0xf0);
  b[0x7b]=/* $D07B */	 (hsync_start>>2)&0xff;
  b[0x7c]=/* $D07C */	 ((hsync_start>>10)&0xf)
    + (hsync_polarity?0x10:0)
    + (vsync_polarity?0x20:0);

  // Horizontal and vertical scaling
  float xscale=hpixels/(640.0+80+80);
  int xscale_120=120/xscale;

  // Side and top-border sizes 
  int screen_width=xscale*640;
  int side_border_width=(hpixels-screen_width)/2;

  b[0x5a]=xscale_120;
  b[0x5c]=side_border_width & 0xff;
  b[0x5d]=(side_border_width >> 8)&0x3f;
  b[0x5e]=xscale;
  
  fprintf(stderr,"Assembled mode with hfreq=%.2fKHz, vfreq=%.2fHz (hwidth=%d), vsync=%d rasters, %dx vertical scale.\n",
	  100000000.0/hwidth,100000000.0/hwidth/vheight,hwidth,
	  vheight-vpixels-vsync_delay,rasters_per_vicii_raster);
  fprintf(stderr,"  xscale=%.2fx (%d/120), side borders %d pixels each.\n",
	  xscale,xscale_120,side_border_width);
  
  return 0;
}

void parse_video_mode(int b[0x80])
{
  int vsync_delay=b[0x72];
  int hsync_end=(((b[0x73]&0xf)<<8)+b[4])<<2;
  int hpixels=(b[0x75]+((b[0x77]&0xf)<<8))<<2;
  int hwidth=(b[0x76]+((b[0x77]&0xf0)<<4))<<2;
  int vpixels=b[0x78]+((b[0x7a]&0xf)<<8);
  int vheight=b[0x79]+((b[0x7a]&0xf0)<<4);
  int hsync_start=(b[0x7b]+((b[0x7c]&0xf)<<8))<<2;
  int hsync_polarity=b[0x7c]&0x10;
  int vsync_polarity=b[0x7c]&0x20;
  int rasters_per_vicii_raster=((b[0x73]&0xf0)>>4)+1;
  
  float pixelclock=100000000;
  float frame_hertz=pixelclock/(hwidth*vheight);
  float hfreq=pixelclock/hwidth/1000.0;
  
  fprintf(stderr,"Video mode is %dx%d pixels, %dx%d frame, sync=%c/%c, vertical scale=%dx, frame rate=%.1fHz, hfreq=%.3fKHz.\n",
	  hpixels,vpixels,hwidth,vheight,
	  hsync_polarity ? '-' : '+',
	  vsync_polarity ? '-' : '+',
	  rasters_per_vicii_raster,
	  frame_hertz,hfreq);
  fprintf(stderr,"   hpixels=$%04x (%d) $D075,$D077.0-3\n",
	  hpixels,hpixels);
  fprintf(stderr,"   hwidth=$%04x (%d) $D076,$D077.7-4\n",
	  hwidth,hwidth);
  fprintf(stderr,"   vpixels=$%04x (%d) $D078,$D07A.0-3\n",
	  vpixels,vpixels);
  fprintf(stderr,"   vsync=$%04x (%d) - $%04x (%d)\n",
	  vpixels+vsync_delay,vpixels+vsync_delay,
	  vheight,vheight);
  fprintf(stderr,"   hsync=$%04x (%d) -- $%04x (%d)\n",
	  hsync_start,hsync_start,
	  hsync_end,hsync_end);

  
  
  return;
}

int viciv_mode_report(unsigned char *r)
{
  fprintf(stderr,"VIC-IV set the video mode to:\n");
  
  // First report on $D072-$D07C modeline
  int b[128];
  int i;
  for(i=0;i<128;i++) b[i]=r[i];
  parse_video_mode(b);

  // Get border positions
  int top_border=(r[0x48]+((r[0x49]&0xf)<<8))&0xfff;
  int bottom_border=(r[0x4a]+((r[0x4b]&0xf)<<8))&0xfff;
  int chargen_start=(r[0x4c]+((r[0x4d]&0xf)<<8))&0xfff;
  int left_border=((r[0x5c]+(r[0x5d]<<8))&0x3fff);
  int right_border=((r[0x5e]+(r[0x5f]<<8))&0x3fff);
  int hscale=r[0x5a];
  int vscale=r[0x5b]+1;
  int xpixels=(r[0x75]+((r[0x77]&0xf)<<8))<<2;
  int ypixels=(r[0x78]+((r[0x7a]&0xf)<<8));

  fprintf(stderr,"Display is %dx%d pixels\n",xpixels,ypixels);
  fprintf(stderr,"  Side borders are %d and %d pixels wide @ $%x and $%x\n",
	  left_border,right_border,left_border,right_border);
  fprintf(stderr,"  Top borders are %d and %d pixels high\n",
	  top_border,ypixels-bottom_border);
  fprintf(stderr,"  Character generator begins at postion %d\n",
	  chargen_start);
  fprintf(stderr,"  Scale = %d/120ths (%.2f per pixel) horizontally and %dx vertically\n",hscale,120.0/hscale,vscale);
	  
  
  return 0;
}


typedef struct {
  char *name;
  char *line;
} modeline_t;

// Modeline table "Modeline" word must have correct case, because these strings can't be mutated.
modeline_t modelines[]={
  // The primary modes we expect for HD out
  {"1920x1200@60","Modeline \"1920x1200\" 151.138 1920 1960 1992 2040 1200 1201 1204 1232 -hsync"},
  {"1920x1080@50","Modeline \"1920x1080\" 148.50 1920 2448 2492 2640 1080 1084 1089 1125 +HSync +VSync"},
  {"1920x1080@60","Modeline \"1920x1080\" 148.35 1920 2008 2052 2200 1080 1084 1089 1125 +HSync +VSync"},

  // Need modes for 800x480 50Hz and 60Hz for MEGAphone. LCD panel limit is 50MHz
  // Totally untested on any monitor
  {"800x480@50","Modeline \"800x480\" 24.13 800 832 920 952 480 490 494 505 +hsync"},
  {"800x480@60","Modeline \"800x480\" 29.59 800 870 0 962 480 490 495 505 +hsync"},
  
  // Some lower resolution modes
  {"800x600@50","Modeline \"800x600\" 30 800 814 0 960 600 601 606 625 +hsync +vsync"},
  {"800x600@60","Modeline \"800x600\" 40 800 840 0 1056 600 601 605 628 +HSync +VSync "},
  
  {NULL,NULL}
};

int prepare_modeline(char *modeline)
{
  // Parse something like:
  // Modeline "1920x1200" 151.138 1920 1960 1992 2040 1200 1201 1204 1232 -hsync  
  
  char opt1[1024]="",opt2[1024]="";
  float pixel_clock_mhz;
  int hpixels,hsync_start,hsync_end,hwidth;
  int vpixels,vsync_start,vsync_end,vheight;
  int hsync_polarity=0;
  int vsync_polarity=0;

  // Add some modeline short cuts
  if (strncasecmp(modeline,"modeline ",9)) {
    int i;
    for(i=0;modelines[i].name;i++)
      if (!strcasecmp(modelines[i].name,modeline)) break;
    if (!modelines[i].name) {
      fprintf(stderr,"Modeline must be a valid Xorg style modeline, or one of the following short-cuts:\n");
      for(i=0;modelines[i].name;i++)
	fprintf(stderr,"  %s = '%s'\n",modelines[i].name,modelines[i].line);
      usage();
    } else
      modeline=modelines[i].line;
  }
  
  fprintf(stderr,"Parsing [%s] as modeline\n",modeline);
  if (modeline[0]=='m') modeline[4]='M';
  if (modeline[4]=='L') modeline[4]='l';
  int fields=sscanf(modeline,"Modeline %*[^ ] %f %d %d %d %d %d %d %d %d %s %s",
		    &pixel_clock_mhz,
		    &hpixels,&hsync_start,&hsync_end,&hwidth,
		    &vpixels,&vsync_start,&vsync_end,&vheight,
		    opt1,opt2);

  if (fields<9)
    {
      fprintf(stderr,"ERROR: Could only parse %d of 9 fields.\n",fields);
      usage();
      return -1;
    }
  else
    {
      int pixel_clock=pixel_clock_mhz*1000000;
      int rasters_per_vicii_raster=(vpixels-80)/200;
      int b[128];

      if (!strcasecmp("-hsync",opt1)) hsync_polarity=1;
      if (!strcasecmp("-hsync",opt2)) hsync_polarity=1;
      if (!strcasecmp("-vsync",opt1)) vsync_polarity=1;
      if (!strcasecmp("-vsync",opt2)) vsync_polarity=1;
      
      assemble_modeline(b,pixel_clock,hpixels,hwidth,vpixels,vheight,
			hsync_polarity,vsync_polarity,
			vsync_start,vsync_end,
			hsync_start,hsync_end,
			rasters_per_vicii_raster);

      snprintf(modeline_cmd,1024,
	       // Main modeline parameters
	       "\nsffd3072 %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n"
	       // X pixel scaling
	       "sffd305a %02x\n"
	       // Side border width
	       "sffd305c %02x %02x %02x\n"
	       ,
	       b[0x72],b[0x73],b[0x74],b[0x75],b[0x76],
	       b[0x77],b[0x78],b[0x79],b[0x7a],b[0x7b],b[0x7c],
	       b[0x5a],
	       b[0x5c],b[0x5d],b[0x5e]
	       );

      parse_video_mode(b);
      
    }

  return 0;
}

#ifdef WINDOWS

void print_error(const char * context)
{
  DWORD error_code = GetLastError();
  char buffer[256];
  DWORD size = FormatMessageA(
			      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_MAX_WIDTH_MASK,
			      NULL, error_code, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
			      buffer, sizeof(buffer), NULL);
  if (size == 0) { buffer[0] = 0; }
  fprintf(stderr, "%s: %s\n", context, buffer);
}
 

// Opens the specified serial port, configures its timeouts, and sets its
// baud rate.  Returns a handle on success, or INVALID_HANDLE_VALUE on failure.
HANDLE open_serial_port(const char * device, uint32_t baud_rate)
{
  HANDLE port = CreateFileA(device, GENERIC_READ | GENERIC_WRITE, 0, NULL,
			    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (port == INVALID_HANDLE_VALUE)
    {
      print_error(device);
      return INVALID_HANDLE_VALUE;
    }
 
  // Flush away any bytes previously read or written.
  BOOL success = FlushFileBuffers(port);
  if (!success)
    {
      print_error("Failed to flush serial port");
      CloseHandle(port);
      return INVALID_HANDLE_VALUE;
    }
 
  // Configure read and write operations to time out after 1 ms and 100 ms, respectively.
  COMMTIMEOUTS timeouts = { 0 };
  timeouts.ReadIntervalTimeout = 0;
  timeouts.ReadTotalTimeoutConstant = 1;
  timeouts.ReadTotalTimeoutMultiplier = 0;
  timeouts.WriteTotalTimeoutConstant = 100;
  timeouts.WriteTotalTimeoutMultiplier = 0;
 
  success = SetCommTimeouts(port, &timeouts);
  if (!success)
    {
      print_error("Failed to set serial timeouts");
      CloseHandle(port);
      return INVALID_HANDLE_VALUE;
    }
 
  DCB state;
  state.DCBlength = sizeof(DCB);
  success = GetCommState(port, &state);
  if (!success)
    {
      print_error("Failed to get serial settings");
      CloseHandle(port);
      return INVALID_HANDLE_VALUE;
    }

  state.fBinary = TRUE;
  state.fDtrControl = DTR_CONTROL_ENABLE;
  state.fDsrSensitivity = FALSE;
  state.fTXContinueOnXoff = FALSE;
  state.fOutX = FALSE;
  state.fInX = FALSE;
  state.fErrorChar = FALSE;
  state.fNull = FALSE;
  state.fRtsControl = RTS_CONTROL_ENABLE;
  state.fAbortOnError = FALSE;
  state.fOutxCtsFlow = FALSE;
  state.fOutxDsrFlow = FALSE;
  state.ByteSize = 8;
  state.StopBits = ONESTOPBIT;
  state.Parity = NOPARITY;
 
  state.BaudRate = baud_rate;
 
  success = SetCommState(port, &state);
  if (!success)
    {
      print_error("Failed to set serial settings");
      CloseHandle(port);
      return INVALID_HANDLE_VALUE;
    }
 
  return port;
}

// Writes bytes to the serial port, returning 0 on success and -1 on failure.
int serialport_write(HANDLE port, uint8_t * buffer, size_t size)
{
  DWORD written;
  //  printf("Calling WriteFile(%d)\n",size);
  BOOL success = WriteFile(port, buffer, size, &written, NULL);
  //  printf("  WriteFile() returned.\n");
  if (!success)
    {
      print_error("Failed to write to port");
      return -1;
    }
  if (written != size)
    {
      print_error("Failed to write all bytes to port");
      return -1;
    }
  return size;
}
 
// Reads bytes from the serial port.
// Returns after all the desired bytes have been read, or if there is a
// timeout or other error.
// Returns the number of bytes successfully read into the buffer, or -1 if
// there was an error reading.
SSIZE_T serialport_read(HANDLE port, uint8_t * buffer, size_t size)
{
  DWORD received=0;
  //  printf("Calling ReadFile(%I64d)\n",size);
  BOOL success = ReadFile(port, buffer, size, &received, NULL);
  if (!success)
    {
      print_error("Failed to read from port");
      return -1;
    }
  //  printf("  ReadFile() returned. Received %ld bytes\n",received);
  return received;
}

#else
int serialport_write(int fd, uint8_t * buffer, size_t size)
{
  return write(fd,buffer,size);
}

size_t serialport_read(int fd, uint8_t * buffer, size_t size)
{
  return read(fd,buffer,size);
}

void set_serial_speed(int fd,int serial_speed)
{
  fcntl(fd,F_SETFL,fcntl(fd, F_GETFL, NULL)|O_NONBLOCK);
  struct termios t;
  if (serial_speed==230400) {
    if (cfsetospeed(&t, B230400)) perror("Failed to set output baud rate");
    if (cfsetispeed(&t, B230400)) perror("Failed to set input baud rate");
  } else if (serial_speed==2000000) {
    if (cfsetospeed(&t, B2000000)) perror("Failed to set output baud rate");
    if (cfsetispeed(&t, B2000000)) perror("Failed to set input baud rate");
  } else if (serial_speed==1000000) {
    if (cfsetospeed(&t, B1000000)) perror("Failed to set output baud rate");
    if (cfsetispeed(&t, B1000000)) perror("Failed to set input baud rate");
  } else if (serial_speed==1500000) {
    if (cfsetospeed(&t, B1500000)) perror("Failed to set output baud rate");
    if (cfsetispeed(&t, B1500000)) perror("Failed to set input baud rate");
  } else {
    if (cfsetospeed(&t, B4000000)) perror("Failed to set output baud rate");
    if (cfsetispeed(&t, B4000000)) perror("Failed to set input baud rate");
  }
  t.c_cflag &= ~PARENB;
  t.c_cflag &= ~CSTOPB;
  t.c_cflag &= ~CSIZE;
  t.c_cflag &= ~CRTSCTS;
  t.c_cflag |= CS8 | CLOCAL;
  t.c_lflag &= ~(ICANON | ISIG | IEXTEN | ECHO | ECHOE);
  t.c_iflag &= ~(BRKINT | ICRNL | IGNBRK | IGNCR | INLCR |
                 INPCK | ISTRIP | IXON | IXOFF | IXANY | PARMRK);
  t.c_oflag &= ~OPOST;
  if (tcsetattr(fd, TCSANOW, &t)) perror("Failed to set terminal parameters");
  
}
#endif

void *run_boundary_scan(void *argp)
{
  xilinx_boundaryscan(boundary_xdc[0]?boundary_xdc:NULL,
		      boundary_bsdl[0]?boundary_bsdl:NULL,
		      jtag_sensitivity[0]?jtag_sensitivity:NULL);
  return (void *)NULL;
}

#ifndef WINDOWS
#define MAX_THREADS 16
int thread_count=0;
pthread_t threads[MAX_THREADS];
#endif

void download_bitstream(void)
{
  int issue,tag;
  char target[1024]="mega65r2";
  if (sscanf(bitstream,"@%d/%d/%s",&issue,&tag,target)<2) {
    fprintf(stderr,"ERROR: @ directive to download bitstreams must be in the format issue/tag/hardware, e.g., 168/1/mega65r2\n");
    exit(-3);
  }

  char filename[8192];
  snprintf(filename,8192,"%s/.netrc",getenv("HOME"));
  FILE *nf=fopen(filename,"rb");
  if (!nf) {
    fprintf(stderr,"WARNING: You don't have a .netrc file.  You probably want to set one up with something like this:\n"
	    "    machine  app.scryptos.com\n"
	    "    login    <ask deft for one>\n"
	    "    password <ask deft for one>\n"
	    "So that you don't get asked by cadaver all the time for a username and password.\n");
  }
  fclose(nf);
  snprintf(filename,8192,"/usr/bin/cadaver");
  nf=fopen(filename,"rb");
  if (!nf) {
    fprintf(stderr,"ERROR: You don't seem to have cadaver installed.\n"
	    "If you are on Ubuntu linux, try:\n"
	    "   sudo apt-get install cadaver\n"
	    "I'll try anyway, in case you just have it installed in a funny place.\n"
	    );
  }
  fclose(nf);
  
  fprintf(stderr,"Fetching bitstream from scryptos archive...\n");
  unlink("/tmp/monitor_load.folder.txt");
  char cmd[8192];
  snprintf(cmd,8192,"echo ls | cadaver \"https://app.scryptos.com/webdav/MEGA/groups/MEGA65%%20filehost/ShareFolder/Bitstreams/Jenkins-Out/mega65-core/issues/%d/\" | grep \"%d-\" | cut -c9-40 | sed 's/ //g' > /tmp/monitor_load.folder.txt",
	   issue,tag);
  system(cmd);

  FILE *f=fopen("/tmp/monitor_load.folder.txt","rb");
  if (!f) {
    fprintf(stderr,"ERROR: Could not read WebDAV retrieved folder name from /tmp/monitor_load.folder.txt\n");
    exit(-2);
  }
  char folder[8192]="";
  fread(folder,1,8192,f);  
  fclose(f);
  while(folder[0]&&folder[strlen(folder)-1]<' ') folder[strlen(folder)-1]=0;
  fprintf(stderr,"Resolved %d/%d to %d/%s\n",issue,tag,issue,folder);
  
  unlink("/tmp/monitor_load.bit");
  snprintf(cmd,8192,"echo \"get %s.bit /tmp/monitor_load.bit\" | cadaver  \"https://app.scryptos.com/webdav/MEGA/groups/MEGA65%%20filehost/ShareFolder/Bitstreams/Jenkins-Out/mega65-core/issues/%d/%s/\"",target,issue,folder);
  fprintf(stderr,"%s\n",cmd);
  system(cmd);
  bitstream="/tmp/monitor_load.bit";
}

void download_hyppo(void)
{
  int issue,tag;
  if (sscanf(hyppo,"@%d/%d",&issue,&tag)<2) {
    fprintf(stderr,"ERROR: @ directive to download HICKUP.M65 must be in the format issue/tag, e.g., 168/1\n");
    exit(-3);
  }

  char filename[8192];
  snprintf(filename,8192,"%s/.netrc",getenv("HOME"));
  FILE *nf=fopen(filename,"rb");
  if (!nf) {
    fprintf(stderr,"WARNING: You don't have a .netrc file.  You probably want to set one up with something like this:\n"
	    "    machine  app.scryptos.com\n"
	    "    login    <ask deft for one>\n"
	    "    password <ask deft for one>\n"
	    "So that you don't get asked by cadaver all the time for a username and password.\n");
  }
  fclose(nf);
  snprintf(filename,8192,"/usr/bin/cadaver");
  nf=fopen(filename,"rb");
  if (!nf) {
    fprintf(stderr,"ERROR: You don't seem to have cadaver installed.\n"
	    "If you are on Ubuntu linux, try:\n"
	    "   sudo apt-get install cadaver\n"
	    "I'll try anyway, in case you just have it installed in a funny place.\n"
	    );
  }
  fclose(nf);
  
  fprintf(stderr,"Fetching HICKUP.M65 from scryptos archive...\n");
  unlink("/tmp/monitor_load.folder.txt");
  char cmd[8192];
  snprintf(cmd,8192,"echo ls | cadaver \"https://app.scryptos.com/webdav/MEGA/groups/MEGA65%%20filehost/ShareFolder/Bitstreams/Jenkins-Out/mega65-core/issues/%d/\" | grep \"%d-\" | cut -c9-40 | sed 's/ //g' > /tmp/monitor_load.folder.txt",
	   issue,tag);
  system(cmd);

  FILE *f=fopen("/tmp/monitor_load.folder.txt","rb");
  if (!f) {
    fprintf(stderr,"ERROR: Could not read WebDAV retrieved folder name from /tmp/monitor_load.folder.txt\n");
    exit(-2);
  }
  char folder[8192]="";
  fread(folder,1,8192,f);  
  fclose(f);
  while(folder[0]&&folder[strlen(folder)-1]<' ') folder[strlen(folder)-1]=0;
  fprintf(stderr,"Resolved %d/%d to %d/%s\n",issue,tag,issue,folder);
  
  unlink("/tmp/monitor_load.HICKUP.M65");
  snprintf(cmd,8192,"echo \"get HICKUP.M65 /tmp/monitor_load.HICKUP.M65\" | cadaver  \"https://app.scryptos.com/webdav/MEGA/groups/MEGA65%%20filehost/ShareFolder/Bitstreams/Jenkins-Out/mega65-core/issues/%d/%s/\"",issue,folder);
  fprintf(stderr,"%s\n",cmd);
  system(cmd);
  hyppo="/tmp/monitor_load.HICKUP.M65";
}

int main(int argc,char **argv)
{
  start_time=time(0);

  printf("Getting started..\n");
  
  int opt;
  while ((opt = getopt(argc, argv, "14B:b:c:C:d:EFHf:jJ:Kk:Ll:m:MnoprR:Ss:t:T:U:v:V:XZ:")) != -1) {
    switch (opt) {
    case 'X': hyppo_report=1; break;
    case 'K': usedk=1; break;
    case 'Z':
      {
	// Zap (reconfig) FPGA via MEGA65 reconfig registers
	sscanf(optarg,"%x",&zap_addr);
	fprintf(stderr,"Reconfiguring FPGA using bitstream at $%08x\n",zap_addr);
	zap=1;
      }
      break;
    case 'B': sscanf(optarg,"%x",&break_point); break;
    case 'L': if (ethernet_video) { usage(); } else { ethernet_cpulog=1; } break;
    case 'E': if (ethernet_cpulog) { usage(); } else { ethernet_video=1; } break;
    case 'U': flashmenufile=strdup(optarg); break;
    case 'R': romfile=strdup(optarg); break;
    case 'H': halt=1; break;
    case 'C': charromfile=strdup(optarg); break;
    case 'c': colourramfile=strdup(optarg); break;
    case '4': do_go64=1; break;
    case '1': comma_eight_comma_one=1; break;
    case 'p': pal_mode=1; break;
    case 'n': ntsc_mode=1; break;
    case 'F': reset_first=1; break; 
    case 'r': do_run=1; break;
    case 'f': fpga_serial=strdup(optarg); break;
    case 'l': serial_port=strdup(optarg); break;
    case 'm': prepare_modeline(optarg); mode_report=1; break;
    case 'M': mode_report=1; break;
    case 'o': osk_enable=1; break;
    case 'd': virtual_f011=1; d81file=strdup(optarg); break;
    case 's':
      serial_speed=atoi(optarg);
      switch(serial_speed) {
      case 1000000:
      case 1500000:
      case 4000000:
      case 230400: case 2000000: break;
      default: usage();
      }
      break;
    case 'S':
      screen_shot=1;
      break;
    case 'b':
      bitstream=strdup(optarg);
      if (bitstream[0]=='@') download_bitstream();
      break;
    case 'v':
      vivado_exe=strdup(optarg);
      break;
    case 'j':
      jtag_only=1; break;
    case 'J':
      boundary_scan=1;
      sscanf(optarg,"%[^,],%[^,],%s",boundary_xdc,boundary_bsdl,jtag_sensitivity);
      break;
    case 'V':
      set_vcd_file(optarg);
      break;
    case 'k':
      hyppo=strdup(optarg);
      if (hyppo[0]=='@') download_hyppo();
      break;
    case 't': case 'T':
      type_text=strdup(optarg);
      if (opt=='T') type_text_cr=1;
      break;
    default: /* '?' */
      usage();
    }
  }    
  
  // Detect only A7100T parts
  // XXX Will require patching for MEGA65 R1 PCBs, as they have an A200T part.
  init_fpgajtag(NULL, bitstream, 0x3631093); // 0xffffffff);
  
  if (boundary_scan) {
#ifdef WINDOWS
    fprintf(stderr,"ERROR: threading on Windows not implemented.\n");
    exit(-1);
#else
    // Launch boundary scan in a separate thread, so that we can monitor signals while
    // running other operations.
    if (pthread_create(&threads[thread_count++], NULL, run_boundary_scan, NULL))
      perror("Failed to create JTAG boundary scan thread.\n");
    else 
      fprintf(stderr,"JTAG boundary scan launched in separate thread.\n");
#endif
  }

  if (jtag_only) do_exit(0);
  
  if ((romfile||charromfile)&&(!hyppo)) {
    fprintf(stderr,"-k is required with -R or -C\n");
    usage();
  }
  
  if (argv[optind]) filename=strdup(argv[optind]);
  if (argc-optind>1) usage();
  
  // Load bitstream if file provided
  if (bitstream)
    {
      if (vivado_exe!=NULL)	{
	/*  For Windows we just call Vivado to do the FPGA programming,
	    while we are having horrible USB problems otherwise. */
	FILE *tclfile=fopen("temp.tcl","w");
	if (!tclfile) {
	  fprintf(stderr,"ERROR: Could not create temp.tcl");
	  exit(-1);
	}
	fprintf(tclfile,
		"open_hw_manager\n"
		"connect_hw_server -allow_non_jtag\n"
		"open_hw_target\n"
		"current_hw_device [get_hw_devices xc7a100t_0]\n"
		"refresh_hw_device -update_hw_probes false [lindex [get_hw_devices xc7a100t_0] 0]\n"
		"refresh_hw_device -update_hw_probes false [lindex [get_hw_devices xc7a100t_0] 0]\n"
		"\n"
		"set_property PROBES.FILE {} [get_hw_devices xc7a100t_0]\n"
		"set_property FULL_PROBES.FILE {} [get_hw_devices xc7a100t_0]\n"
		"set_property PROGRAM.FILE {%s} [get_hw_devices xc7a100t_0]\n"
		"refresh_hw_device -update_hw_probes false [lindex [get_hw_devices xc7a100t_0] 0]\n"
		"program_hw_devices [get_hw_devices xc7a100t_0]\n"
		"refresh_hw_device [lindex [get_hw_devices xc7a100t_0] 0]\n"
		"quit\n",bitstream);
	fclose(tclfile);
	char cmd[8192];
	snprintf(cmd,8192,"%s -mode batch -nojournal -nolog -notrace -source temp.tcl",
		 vivado_exe);
	printf("Running %s...\n",cmd);
	system(cmd);
	unlink("temp.tcl");
      } else {
	// No Vivado.exe, so try to use internal fpgajtag implementation.
	if (fpga_serial) {
	  fpgajtag_main(bitstream,fpga_serial);
	}
	else {
	  fpgajtag_main(bitstream,NULL);
	}
      }
#ifdef WINDOWS
      fprintf(stderr,"[T+%I64dsec] Bitstream loaded\n",(long long)time(0)-start_time);
#else    
      fprintf(stderr,"[T+%lldsec] Bitstream loaded\n",(long long)time(0)-start_time);
#endif
    }

  if (virtual_f011) {
    if ((!bitstream)||(!hyppo)) {
      fprintf(stderr,"ERROR: -d requires -b and -k to also be specified.\n");
      exit(-1);
    }
#ifdef WINDOWS
    fprintf(stderr,"[T+%I64dsec] Remote access to disk image '%s' requested\n",(long long)time(0)-start_time,d81file);
#else
    fprintf(stderr,"[T+%lldsec] Remote access to disk image '%s' requested\n",(long long)time(0)-start_time,d81file);
#endif
    
  }

#ifdef WINDOWS
  fd=open_serial_port(serial_port,2000000);
  if (fd==INVALID_HANDLE_VALUE) {
    fprintf(stderr,"Could not open serial port '%s'\n",serial_port);
    exit(-1);
  }
  
#else
  errno=0;
  fd=open(serial_port,O_RDWR);
  if (fd==-1) {
    fprintf(stderr,"Could not open serial port '%s'\n",serial_port);
    perror("open");
    exit(-1);
  }

  set_serial_speed(fd,serial_speed);
#endif

  printf("Calling monitor_sync()\n");
  monitor_sync();

  printf("Calling detect_mode()\n");
  detect_mode();

  printf("Progressing...\n");
  
#ifndef WINDOWS
  if (virtual_f011&&serial_speed==2000000) {
    // Try bumping up to 4mbit
    slow_write(fd,"\r+9\r",4);
    set_serial_speed(fd,4000000);
    serial_speed=4000000;
  }
#endif

  if (screen_shot) {
    exit(do_screen_shot());
  }
  
  unsigned long long last_check = gettime_ms();
  int phase=0;

  while(1)
    {
      int b;
      int fast_mode=0;
      char read_buff[1024];
      switch(state) {
      case 0: case 2: case 3: case 99:
	errno=0;
	b=serialport_read(fd,(unsigned char *)read_buff,1024);
	if (b>0) {
	  //printf("%s\n", read_buff);
	  int i;
	  for(i=0;i<b;i++) {
	    process_char(read_buff[i],1);
	  }
	} else {
	  usleep(1000);
	}

	//        fast_mode = saw_c65_mode || saw_c64_mode;
	if (gettime_ms()>last_check) {
	  //          if(fast_mode) {
	  //	  } else
	  {
	    if (state==99) printf("sending R command to sync @ %dpbs.\n",serial_speed);
	    if (hyppo_report) {
	      switch (phase%(5+hypervisor_paused)) {
	      case 0: slow_write_safe(fd,"Mfffbc00\r",9); break;
	      case 1: slow_write_safe(fd,"Mfffbd00\r",9); break;
	      case 2: slow_write_safe(fd,"Mfffbe00\r",9); break;
	      case 3: slow_write_safe(fd,"Mfffbf00\r",9); break;
	      }
	    } else {
	      switch (phase%(5+hypervisor_paused)) {
	      case 0: slow_write_safe(fd,"r\r",2); break; // PC check
	      case 1: slow_write_safe(fd,"m800\r",5); break; // C65 Mode check
	      case 2: slow_write_safe(fd,"m42c\r",5); break; // C64 mode check
	      case 3: slow_write_safe(fd,"mffd3077\r",9); break; 
	      case 4:
		// Requests screen address if we are taking a screenshot
		//		if (screen_shot) slow_write_safe(fd,"Mffd3058\r",9);
		break;
	      case 5: slow_write_safe(fd,"mffd3659\r",9); break; // Hypervisor virtualisation/security mode flag check
	      default: phase=0;
	      }
	    }
	  } 
	  phase++;	  
	  last_check=gettime_ms()+ (fast_mode ? 5 : 50);
	}
	break;
      case 1: // trapped LOAD, so read file name
	slow_write(fd,"mb7\r",4);
	state=0;
	break;
      default:
	usleep(1000);	
      }
    }
    
  do_exit(0);
}
 
void do_exit(int retval) {
#ifndef WINDOWS
  printf("Background tasks running. CONTROL+C to stop...\n");
  for(int i=0;i<thread_count;i++)
    pthread_join(threads[i], NULL);     
#endif  
  exit(retval);
}