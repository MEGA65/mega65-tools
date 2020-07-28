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
#include "m65.h"
#include "screen_shot.h"

#define SLOW_FACTOR 1
#define SLOW_FACTOR2 1

#ifdef WINDOWS
#include <windows.h>
#undef SLOW_FACTOR
#define SLOW_FACTOR 1
#define SLOW_FACTOR2 1
// #define do_usleep usleep
void do_usleep(__int64 usec) 
{ 
  HANDLE timer; 
  LARGE_INTEGER ft; 

  ft.QuadPart = -(10*usec); // Convert to 100 nanosecond interval, negative value indicates relative time

  timer = CreateWaitableTimer(NULL, TRUE, NULL); 
  SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0); 
  WaitForSingleObject(timer, INFINITE); 
  CloseHandle(timer); 
}

#else
#include <termios.h>
#define do_usleep usleep
#endif

#ifdef __APPLE__
#include <sys/ioctl.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/serial/IOSerialKeys.h>
#include <IOKit/serial/ioss.h>
#include <IOKit/IOBSD.h>
#endif


#ifdef WINDOWS

#define bzero(b,len) (memset((b), '\0', (len)), (void) 0)  
#define bcopy(b1,b2,len) (memmove((b2), (b1), (len)), (void) 0)

FILE iobs[3];

FILE *__imp___acrt_iob_func(void)
{
  iobs[0]=*stdin;
  iobs[1]=*stdout;
  iobs[2]=*stderr;
  return iobs;
}
#endif

#ifdef __APPLE__
static const int B1000000 = 1000000;
static const int B1500000 = 1500000;
static const int B2000000 = 2000000;
static const int B4000000 = 4000000;
#endif
static time_t start_time=0;

int osk_enable=0;

int not_already_loaded=1;

int halt=0;

int usedk=0;


// 0 = old hard coded monitor, 1= Kenneth's 65C02 based fancy monitor
// Only REALLY old bitstreams don't have the new monitor
int new_monitor=1;

int viciv_mode_report(unsigned char *viciv_regs);

int fetch_ram(unsigned long address,unsigned int count,unsigned char *buffer);
int push_ram(unsigned long address,unsigned int count,unsigned char *buffer);
int do_screen_shot(void);
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
  fprintf(stderr,"MEGA65 cross-development tool.\n");
  fprintf(stderr,"usage: m65 [-l <serial port>] [-s <230400|2000000|4000000>]  [-b <FPGA bitstream> [-v <vivado.bat>] [[-k <hickup file>] [-R romfile] [-U flashmenufile] [-C charromfile]] [-c COLOURRAM.BIN] [-B breakpoint] [-o] [-d diskimage.d81] [-j] [-J <XDC,BSDL[,sensitivity list]> [-V <vcd file>]] [[-1] [<-t|-T> <text>] [-f FPGA serial ID] [filename]] [-H] [-E|-L] [-Z <flash addr>]\n");
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
extern PORT_TYPE fd;
#else
extern PORT_TYPE fd;
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
char *vivado_bat=NULL;
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

void timestamp_msg(char *msg)
{
  if (!start_time) start_time=time(0);
#ifdef WINDOWS
  fprintf(stderr,"[T+%I64dsec] %s",(long long)time(0)-start_time,msg);
#else
  fprintf(stderr,"[T+%lldsec] %s",(long long)time(0)-start_time,msg);
#endif

  return;
}


int slow_write(PORT_TYPE fd,char *d,int l)
{
  // UART is at 2Mbps, but we need to allow enough time for a whole line of
  // writing. 100 chars x 0.5usec = 500usec. So 1ms between chars should be ok.
  int i;
#if 0
  printf("\nWriting ");
  for(i=0;i<l;i++)
  {
    if (d[i]>=' ') printf("%c",d[i]); else printf("[$%02X]",d[i]);
  }
  printf("\n");
  char line[1024];
  fgets(line,1024,stdin);
#endif

  for(i=0;i<l;i++)
  {
    if (serial_speed==4000000) do_usleep(1000*SLOW_FACTOR); else do_usleep(2000*SLOW_FACTOR);
    int w=serialport_write(fd,(unsigned char *)&d[i],1);
    while (w<1) {
      if (serial_speed==4000000) do_usleep(500*SLOW_FACTOR); else do_usleep(1000*SLOW_FACTOR);
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
  if (!cpu_stopped)
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
  if (cpu_stopped) {
    printf("CPU already stopped.\n");
    return 1;
  }
  // Stop CPU
  printf("Stopping CPU\n");
  do_usleep(50000);
  slow_write(fd,"t1\r",3);
  cpu_stopped=1;
  return 0;
}

int start_cpu(void)
{
  // Stop CPU
  if (cpu_stopped) {
    timestamp_msg("");
    fprintf(stderr,"Starting CPU\n");
  }
  do_usleep(50000);
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

  do_usleep(50000);
  unsigned char buf[65536];
  int max_bytes;
  int byte_limit=4096;
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

#ifdef WINDOWS_GUS
    // Windows doesn't seem to work with the l fast-load monitor command
    printf("Asking Gus to write data...\n");
    for(int i=0;i<b;i+=16) {
      int ofs=0;
      sprintf(cmd,"s%x",load_addr+i); ofs=strlen(cmd);
      for(int j=0;(j<16)&&(i+j)<b;j++) { sprintf(&cmd[ofs]," %x",buf[i+j]); ofs=strlen(cmd); }
      sprintf(&cmd[ofs],"\r"); ofs=strlen(cmd);
      slow_write(fd,cmd,strlen(cmd));
    }
#else
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
    do_usleep(5000*SLOW_FACTOR);
    int n=b;
    unsigned char *p=buf;
    while(n>0) {
      int w=serialport_write(fd,p,n);
      if (w>0) { p+=w; n-=w; } else do_usleep(1000*SLOW_FACTOR);
    }
    if (serial_speed==230400) do_usleep(10000+50*b*SLOW_FACTOR);
    else if (serial_speed==2000000)
      // 2mbit/sec / 11bits/char (inc space) = ~5.5usec per char
      do_usleep(6*b*SLOW_FACTOR2);
    else
      // 4mbit/sec / 11bits/char (inc space) = ~2.6usec per char
      do_usleep(3*b*SLOW_FACTOR2);
#endif

    load_addr+=b;

    max_bytes=0x10000-(load_addr&0xffff);
    if (max_bytes>byte_limit) max_bytes=byte_limit;
    b=fread(buf,1,max_bytes,f);	  
  }

  fclose(f);
  char msg[1024];
  snprintf(msg,1024,"File '%s' loaded.\n",filename);
  timestamp_msg(msg);
  return 0;
}

int mega65_poke(unsigned int addr,unsigned char value)
{
  return push_ram(addr,1,&value);
}

unsigned char mega65_peek(unsigned int addr)
{
  unsigned char b;
  fetch_ram(addr,1,&b);
  return b;
}


int restart_hyppo(void)
{
  // Start executing in new hyppo
  if (!halt) {
    printf("Re-Starting CPU in new HYPPO\n");
    do_usleep(50000);
    slow_write(fd,"g8100\r",6);
    do_usleep(10000);
    slow_write(fd,"t0\r",3);
    cpu_stopped=0;
  }
  return 0;
}

void print_spaces(FILE *f,int col)
{
  for(int i=0;i<col;i++)
    printf(" ");  
}

int dump_bytes(int col, char *msg,unsigned char *bytes,int length)
{
  print_spaces(stderr,col);
  printf("%s:\n",msg);
  for(int i=0;i<length;i+=16) {
    print_spaces(stderr,col);
    printf("%04X: ",i);
    for(int j=0;j<16;j++) if (i+j<length) printf(" %02X",bytes[i+j]); else printf("   ");
    printf(" | ");
    for(int j=0;j<16;j++) if (i+j<length) {
      if (bytes[i+j]>=0x20&&bytes[i+j]<0x7f) {
        printf("%c",bytes[i+j]);
      } else printf(".");
    }
    printf("\n");
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

  timestamp_msg("Injecting string into key buffer at ");
  fprintf(stderr,"$%04X : ",buffer_addr);
  for(int i=0;s[i];i++) {
    if (s[i]>=' '&&s[i]<0x7c) fprintf(stderr,"%c",s[i]); else fprintf(stderr,"[$%02x]",s[i]);    
  }
  fprintf(stderr,"\n");

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
        do_usleep(1000*SLOW_FACTOR);
        int n=0x200;
        unsigned char *p=buf;
        //	      fprintf(stderr,"%s\n",cmd);
        //	      dump_bytes(0,"F011 virtual sector data",p,512);
        while(n>0) {
          int w=serialport_write(fd,p,n);
          if (w>0) { p+=w; n-=w; } else do_usleep(1000*SLOW_FACTOR);
        }
        if (serial_speed==230400) do_usleep(10000+50*b*SLOW_FACTOR);
        else do_usleep(10000+6*b*SLOW_FACTOR);
#ifdef WINDOWS       
        printf("T+%I64d ms : Block sent.\n",gettime_ms()-start);
#else	
        printf("T+%lld ms : Block sent.\n",gettime_ms()-start);
#endif	
      }
    }

  }

  /* signal done/result */
  mega65_poke(0xffd3068,side);

  timestamp_msg("Finished V-FDC read.\n");

  return 0;
}


void show_hyppo_report(void)
{
  // Buffer starats at $BC00 in HYPPO
  // $BC00 - $BCFF = DOS work area
  // $BD00 - $BDFF = Process Descriptor
  // $BE00 - $BEFF = Stack
  // $BF00 - $BFFF = ZP

  unsigned char syspart_buffer[0x40];

  fetch_ram(0xfffbbc0,0x40,syspart_buffer);
  fetch_ram(0xfffbc00,0x400,hyppo_buffer);
  printf("HYPPO status:\n");

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
  printf("SYSPART start = $%02x%02x%02x%02x\n",
      syspart_buffer[3],syspart_buffer[2],syspart_buffer[1],syspart_buffer[0]);

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
  cmd[1]='#'; // prevent instruction stepping
  cmd[2]=0x0d; // Carriage return
  do_usleep(20000); // Give plenty of time for things to settle
  slow_write_safe(fd,cmd,3);
  //  printf("Wrote empty command.\n");
  do_usleep(20000); // Give plenty of time for things to settle
  int b=1;
  // Purge input  
  //  printf("Purging input.\n");
  while(b>0) {
    b=serialport_read(fd,read_buff,8192);
    //    if (b>0) dump_bytes(2,"Purged input",read_buff,b);
  }

  for(int tries=0;tries<10;tries++) {
#ifdef WINDOWS
    snprintf(cmd,1024,"#%08x\r",rand());
#else
    snprintf(cmd,1024,"#%08lx\r",random());
#endif
    //    printf("Writing token: '%s'\n",cmd);
    slow_write_safe(fd,cmd,strlen(cmd));

    for(int i=0;i<10;i++) {
      do_usleep(10000*SLOW_FACTOR);      
      b=serialport_read(fd,read_buff,8192);
      if (b<0) b=0;
      if (b>8191) b=8191;
      read_buff[b]=0;
      //      if (b>0) dump_bytes(2,"Sync input",read_buff,b);

      //      if (b>0) dump_bytes(0,"read_data",read_buff,b);
      if (strstr((char *)read_buff,cmd)) {
        //	printf("Found token. Synchronised with monitor.\n");
        state=99;
        return 0;      
      }
    }
    do_usleep(10000*SLOW_FACTOR);
  }
  printf("Failed to synchronise with the monitor.\n");
  return 1;
}

void progress_to_RTI(void)
{
  int bytes=0;
  int match_state=0;
  int b=0;
  unsigned char buff[8192];
  slow_write_safe(fd,"tc\r",3);
  while(1) {
    b=serialport_read(fd,buff,8192);
    if (b>0) dump_bytes(2,"RTI search input",buff,b);
    if (b>0) {
      bytes+=b;
      buff[b]=0;
      for(int i=0;i<b;i++) {
        if (match_state==0&&buff[i]=='R') { match_state=1; }
        else if (match_state==1&&buff[i]=='T') { match_state=2; } 
        else if (match_state==2&&buff[i]=='I') {
          slow_write_safe(fd,"\r",1);
          fprintf(stderr,"RTI seen after %d bytes\n",bytes);
          return;
        } else match_state=0;
      }
    }
    fflush(stdout);
  }
}

int get_pc(void)
{
  /*
     Get current programme counter value of CPU
     */
  slow_write_safe(fd,"r\r",2);
  do_usleep(50000);
  unsigned char buff[8192];
  int b=serialport_read(fd,buff,8192);
  if (b<0) b=0;
  if (b>8191) b=8191;
  buff[b]=0;
  //  if (b>0) dump_bytes(2,"PC read input",buff,b);
  char *s=strstr((char *)buff,"\n,");
  if (s) return strtoll(&s[6],NULL,16);
  else return -1;
}

int breakpoint_pc=-1;
int breakpoint_set(int pc)
{
  char cmd[8192];
  monitor_sync();
  start_cpu();
  snprintf(cmd,8192,"b%x\r",pc);
  breakpoint_pc=pc;
  slow_write(fd,cmd,strlen(cmd));
  // XXX any t0 or t1 cancels a queued breakpoint,
  // so must be avoided
  return 0;
}

int breakpoint_wait(void)
{
  char read_buff[8192];
  char pattern[16];

  snprintf(pattern,16,"\n,077");

  int match_state=0;

  // Now read until we see the requested PC
  timestamp_msg("");
  fprintf(stderr,"Waiting for breakpoint at $%04X to trigger.\n",breakpoint_pc);
  while(1) {
    int b=serialport_read(fd,(unsigned char *)read_buff,8192);  

    for(int i=0;i<b;i++) {
      if (read_buff[i]==pattern[match_state]) {
        if (match_state==4) {
          timestamp_msg("");
          fprintf(stderr,"Breakpoint @ $%04X triggered.\n",breakpoint_pc);
          slow_write(fd,"t1\r",3);
          cpu_stopped=1;
          //	  printf("stopped following breakpoing.\n");
          return 0;
        } else match_state++;
      } else {
        match_state=0;
      }
    }
    //    if (b>0) dump_bytes(2,"Breakpoint wait input",read_buff,b);

  }

}

int push_ram(unsigned long address,unsigned int count,unsigned char *buffer)
{
  char cmd[8192];
  for(unsigned int offset=0;offset<count;)
  {
    int b=count-offset;      
    // Limit to same 64KB slab
    if (b>(0xffff-((address+offset)&0xffff)))
      b=(0xffff-((address+offset)&0xffff));
    if (b>4096) b=4096;

    monitor_sync();

    if (new_monitor) 
      sprintf(cmd,"l%lx %lx\r",address+offset,(address+offset+b)&0xffff);
    else
      sprintf(cmd,"l%lx %lx\r",address+offset-1,address+offset+b-1);
    slow_write(fd,cmd,strlen(cmd));
    do_usleep(1000*SLOW_FACTOR);
    int n=b;
    unsigned char *p=&buffer[offset];
    while(n>0) {
      int w=serialport_write(fd,p,n);
      if (w>0) { p+=w; n-=w; } else do_usleep(1000*SLOW_FACTOR);
    }
    if (serial_speed==230400) do_usleep(10000+50*b*SLOW_FACTOR);
    else if (serial_speed==2000000)
      // 2mbit/sec / 11bits/char (inc space) = ~5.5usec per char
      do_usleep(6*b*SLOW_FACTOR2);
    else
      // 4mbit/sec / 11bits/char (inc space) = ~2.6usec per char
      do_usleep(3*b*SLOW_FACTOR2);

    offset+=b;
  }
  return 0;
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

#ifdef __CYGWIN__
  monitor_sync();
#endif
  while(addr<(address+count)) {
    if ((address+count-addr)<17) {
      snprintf(cmd,8192,"m%X\r",(unsigned int)addr);
      end_addr=addr+0x10;
    } else {
      snprintf(cmd,8192,"M%X\r",(unsigned int)addr);
      end_addr=addr+0x100;
    }
    //printf("Sending '%s'\n",cmd);
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

          // Don't write more bytes than requested
          if ((addr-address+i)>=count) break;

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

int fetch_ram_invalidate(void)
{
  ram_cache_initialised=0;
  return 0;
}

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
  uint8_t read_buff[8192];
  /*
     Set saw_c64_mode or saw_c65_mode according to what we can discover. 
     We can look at the C64/C65 charset bit in $D030 for a good clue.
     But we also really want to know that the CPU is in the keyboard 
     input loop for either of the modes, if possible. OpenROMs being
     under development makes this tricky.
     */
  saw_c65_mode=0;
  saw_c64_mode=0;

  // flush out any serial data that occurred after the restart.
  serialport_read(fd,read_buff,8192);

  unsigned char mem_buff[8192];
  fetch_ram(0xffd3030,1,mem_buff);
  while(mem_buff[0]&0x01) {
    fprintf(stderr,"Waiting for MEGA65 KERNAL/OS to settle...\n");
    do_usleep(200000);
    fetch_ram(0xffd3030,1,mem_buff);    
  }

  // Wait for HYPPO to exit
  int d054=mega65_peek(0xffd3054);
  while(d054&7) {
    do_usleep(50000);
    d054=mega65_peek(0xffd3054);
  }


  //  printf("$D030 = $%02X\n",mem_buff[0]);
  if (mem_buff[0]==0x64) {
    // Probably C65 mode
    int in_range=0;
    // Allow more tries to allow more time for ROM checksum to finish
    // or boot attempt from floppy to finish
    for (int i=0;i<10;i++) {
      int pc=get_pc();
      if (pc>=0xe1a0&&pc<=0xe1b4) in_range++; else {
        // C65 ROM does checksum, so wait a while if it is in that range
        if (pc>=0xb000&&pc<0xc000) sleep(1);
        // Or booting from internal drive is also slow
        if (pc>=0x9c00&&pc<0x9d00) sleep(1);
        // Or something else it does while booting
        if (pc>=0xfeb0&&pc<0xfed0) sleep(1);
        else {
          //	  fprintf(stderr,"Odd PC=$%04x\n",pc);
          do_usleep(100000);
        }
      }
    }
    if (in_range>3) {
      // We are in C65 BASIC main loop, so assume it is C65 mode
      saw_c65_mode=1;
      timestamp_msg("");
      fprintf(stderr,"CPU in C65 BASIC 10 main loop.\n");
      return 0;
    }
  } else if (mem_buff[0]==0x00) {
    // Probably C64 mode
    int in_range=0;
    for (int i=0;i<5;i++) {
      int pc=get_pc();
      // XXX Might not work with OpenROMs?
      if (pc>=0xe5cd&&pc<=0xe5d5) in_range++;
      else {
        //	printf("Odd PC=$%04x\n",pc);
        usleep(100000);
      }
    }
    if (in_range>3) {
      // We are in C64 BASIC main loop, so assume it is C65 mode
      saw_c64_mode=1;
      timestamp_msg("");
      fprintf(stderr,"CPU in C64 BASIC 2 main loop.\n");
      return 0;
    }
  }
  printf("Could not determine C64/C65/MEGA65 mode.\n");
  return 1;
}


// screen-code to scan-code conversion?
void do_type_key(unsigned char key)
{  
  int c1=0x7f;
  int c2=0x7f;

  // left shift for upper case letters
  if (key>=0x41&&key<=0x5A)
  {
    key += 0x20;  // convert to ascii's lowercase, so the c2 will shift it to upper-case
    c2=0x0f;
  }

  switch (key)
  {
    // Punctuation that requires shifts
    case '!': key='1'; c2=0x0f; break;
    case '\"': key='2'; c2=0x0f; break;
    case '#': key='3'; c2=0x0f; break;
    case '$': key='4'; c2=0x0f; break;
    case '%': key='5'; c2=0x0f; break;
    case '(': key='8'; c2=0x0f; break;
    case ')': key='9'; c2=0x0f; break;
    case '?': key='/'; c2=0x0f; break;
    case '<': key=','; c2=0x0f; break;
    case '>': key='.'; c2=0x0f; break;
  }


  switch (key)
  {	  
    case 0x03: c1=0x3f; break; // RUN/STOP
    case 0x1d: c1=0x02; break; // Cursor right
    case 0x9d: c1=0x02; c2=0x0f; break; // Cursor left
    case 0x11: c1=0x07; break; // Cursor down
    case 0x91: c1=0x07; c2=0x0f; break; // Cursor up
    case 0x0d: c1=0x01; break; // RETURN
    case 0x14: c1=0x00; break; // INST/DEL
    case 0xF1: c1=0x04; break; // F1
    case 0xF3: c1=0x05; break; // F3
    case 0xF5: c1=0x06; break; // F5
    case 0xF7: c1=0x03; break; // F7

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
              // What was this with 0x91?
              //	case 0x91: c1=0x36; break;
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
  //  fprintf(stderr,"keys $%02x $%02x\n",c1,c2);
  snprintf(cmd,1024,"sffd3615 %02x %02x\n",c1,c2);
  slow_write(fd,cmd,strlen(cmd));
  // Stop pressing keys
  slow_write(fd,"sffd3615 7f 7f 7f \n",19);

}

void do_type_text(char *type_text)
{
  fprintf(stderr,"Typing text via virtual keyboard...\n");

  int use_line_mode=0;

  if (!strcmp(type_text,"-")) {
#ifndef WINDOWS
    if (use_line_mode) {
#endif
      fprintf(stderr,"Reading input from stdin.\nType . on a line by itself to end.\n");
      char line[1024];
      line[0]=0; fgets(line,1024,stdin);
      while(line[0]) {
        while(line[0]&&((line[strlen(line)-1]=='\n')||line[strlen(line)-1]=='\r'))
          line[strlen(line)-1]=0;
        if (!strcmp(line,".")) break;

        for(int i=0;line[i];i++) do_type_key(line[i]);

        // carriage return at end of line
        slow_write(fd,"sffd3615 01 7f 7f \n",19);
        slow_write(fd,"sffd3615 7f 7f 7f \n",19);

        line[0]=0; fgets(line,1024,stdin);
      }
#ifndef WINDOWS
    }
    else
    {
      // Char mode

      // XXX Windows needs a quite different approach.
      // See, e.g.: https://cpp.hotexamples.com/examples/-/-/ReadConsoleInput/cpp-readconsoleinput-function-examples.html
      // But probably easier to just add this functionality in to tayger's MEGA65 Connect programme instead.

      struct termios old_tio, new_tio;
      unsigned char c;

      /* get the terminal settings for stdin */
      tcgetattr(STDIN_FILENO,&old_tio);

      /* we want to keep the old setting to restore them a the end */
      new_tio=old_tio;

      /* disable canonical mode (buffered i/o) and local echo */
      new_tio.c_lflag &=(~ICANON & ~ECHO);

      /* set the new settings immediately */
      tcsetattr(STDIN_FILENO,TCSANOW,&new_tio);

      fprintf(stderr,"Reading input from terminal in character mode.\n"
          "- CONTROL-Y = end session.\n"
          "- CONTROL-L = refresh ascii screenshot.\n"
          "- CONTROL-R = reset mega65\n"
          "- CONTROL-F = trigger freeze menu.\n");

      c=getc(stdin);
      while(c!=25) {
        //printf("$%02x -> ",c);
        switch(c) {
          case 0x7f: c=0x14; break; // DELETE
          case 0x0a: c=0x0d; break; // RETURN
          case 0x1b:  // 1st ESCPE
            c=getc(stdin);
            //printf("($%02x - %c) ",c, c);

            if (c==0x1b) { // RUN/STOP (Two escapes)
              c=0x03;
            }
            else if (c==0x4f) { // F1 or F3?
              c = getc(stdin);
              //printf("($%02x - %c) ",c, c);
              if (c == 0x50) c = 0xF1;
              else if (c == 0x52) c = 0xF3;
              else c = 0;
            }
            else if (c==0x5b) {
              c=getc(stdin);
              //printf("($%02x - %c) ",c, c);
              switch(c) {
                case 0x42: c=0x11; break; // down
                case 0x41: c=0x91; break; // up
                case 0x44: c=0x9D; break; // left
                case 0x48: c=0x13; break; // HOME
                case 0x43: c=0x1D; break; // right
                case 'M': c=0x0D; break; // RETURN 
                case 'T': c=0x14; break; // INST/DEL
                case '1':
                  c=getc(stdin);
                  //printf("($%02x - %c) ",c, c);
                  switch(c) {
                  case 'P': c=0xF1; break; // F1
                  case 'R': c=0xF3; break; // F3
                  case '5': c=0xF5; break; // F5
                  case '8': c=0xF7; break; // F7
                  default: c = 0; break;
                  }
                  break;
                default:
                  c=0;
              }
            }
            else
              c = 0;
      }
      //printf("$%02x\n",c);
      if (c) {
        switch(c)
        {
          case 0x0c:  // CTRL-L = trigger another screenshot
            printf("\nrefresh!\n");
            get_video_state();
            do_screen_shot_ascii();
            break;

          case 0x06:  // CTRL-F = trigger freeze menu
            printf("\nfreezing...\n");
            char scmd[10];
            slow_write(fd,"sffd3615 52 7f 7f \n",19);   // hold down restore
            sleep(1);
            slow_write(fd,"sffd3615 7f 7f 7f \n",19);   // release down restore
            monitor_sync();
            sleep(1);
            get_video_state();
            do_screen_shot_ascii();
            printf("freeze completed!\n");
            break;

          case 0x12:  // CTRL-R = reset mega65
            printf("\nresetting...\n");

            start_cpu(); slow_write(fd,"\r!\r",3); monitor_sync(); sleep(2);
            get_video_state();
            do_screen_shot_ascii();
            printf("reset completed!\n");
            break;

          default:
            do_type_key(c);
            break;
        }

        if (c==0x0d)
        {
          get_video_state();
          do_screen_shot_ascii();
        }
        else
        {
          printf("%c", c);
        }
      } else usleep(1000);
      c=getc(stdin);
    }
    /* enable canonical mode (buffered i/o) and local echo */
    new_tio.c_lflag |=(ICANON | ECHO);

    /* set the new settings immediately */
    tcsetattr(STDIN_FILENO,TCSANOW,&new_tio);

  }
#endif      
} else {  
  int i;
  for(i=0;type_text[i];i++) {
    if (type_text[i]=='~') {
      unsigned char c1;
      // control sequences
      switch (type_text[i+1])
      {
        case 'C': c1=0x03; break;              // RUN/STOP
        case 'D': c1=0x11; break;              // down
        case 'U': c1=0x91; break;     // up
        case 'L': c1=0x9D; break;              // left
        case 'H': c1=0x13; break;              // HOME
        case 'R': c1=0x1D; break;     // right
        case 'M': c1=0x0D; break;              // RETURN 
        case 'T': c1=0x14; break;              // INST/DEL
        case '1': c1=0xF1; break; // F1
        case '3': c1=0xF3; break; // F3
        case '5': c1=0xF5; break; // F5
        case '7': c1=0xF7; break; // F7
      }
      do_type_key(c1);
      i++;
    }
    else
      do_type_key(type_text[i]);
  }

  // RETURN at end if requested
  if (type_text_cr)
    slow_write(fd,"sffd3615 01 7f 7f \n",19);
}
// Stop pressing keys
slow_write(fd,"sffd3615 7f 7f 7f \n",19);
}



char line[1024];
int line_len=0;



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
  DWORD offset=0;
  DWORD written;
  BOOL success;
  //  printf("Calling WriteFile(%d)\n",size);

  while(offset<size) {  
    success = WriteFile(port, &buffer[offset], size - offset, &written, NULL);
    //  printf("  WriteFile() returned.\n");
    if (!success)
    {
      print_error("Failed to write to port");
      return -1;
    }
    if (written>0) offset+=written;
    if (offset<size) {
      // Assume buffer is full, so wait a little while
      usleep(1000);
    }
  }
  success = FlushFileBuffers(port);
  if (!success) print_error("Failed to flush buffers"); 
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
#ifdef __APPLE__
  return write(fd,buffer,size);
#else
  size_t offset=0;
  while(offset<size) {
    int written=write(fd,&buffer[offset],size-offset);
    if (written>0) offset+=written;
    if (offset<size) { usleep(1000);
      //      printf("Wrote %d bytes\n",written);
    }
  }
#endif
  return size;
}

size_t serialport_read(int fd, uint8_t * buffer, size_t size)
{
  return read(fd,buffer,size);
}

void set_serial_speed(int fd,int serial_speed)
{
  fcntl(fd,F_SETFL,fcntl(fd, F_GETFL, NULL)|O_NONBLOCK);
  struct termios t;

  if (fd<0) {
    fprintf(stderr,"WARNING: serial port file descriptor is -1\n");    
  }

#ifdef __APPLE__
  speed_t speed = serial_speed;
  fprintf(stderr,"Setting serial speed to %d bps using OSX method.\n",speed); 
  if (ioctl(fd, IOSSIOSPEED, &speed) == -1) {
    perror("Failed to set output baud rate using IOSSIOSPEED");
  }
  if (tcgetattr(fd, &t)) perror("Failed to get terminal parameters");
  cfmakeraw(&t);
  if (tcsetattr(fd, TCSANOW, &t)) perror("Failed to set OSX terminal parameters");  
#else  
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
  }
#ifndef __CYGWIN__
  else {
    if (cfsetospeed(&t, B4000000)) perror("Failed to set output baud rate");
    if (cfsetispeed(&t, B4000000)) perror("Failed to set input baud rate");
  }
#endif

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
#endif

}
#endif

void *run_boundary_scan(void *argp)
{
  //xilinx_boundaryscan(boundary_xdc[0]?boundary_xdc:NULL,
  //	      boundary_bsdl[0]?boundary_bsdl:NULL,
  //	      jtag_sensitivity[0]?jtag_sensitivity:NULL);
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
  char folder[1024]="";
  fread(folder,1,1024,f);  
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
  char folder[1024]="";
  fread(folder,1,1024,f);  
  fclose(f);
  while(folder[0]&&folder[strlen(folder)-1]<' ') folder[strlen(folder)-1]=0;
  fprintf(stderr,"Resolved %d/%d to %d/%s\n",issue,tag,issue,folder);

  unlink("/tmp/monitor_load.HICKUP.M65");
  snprintf(cmd,8192,"echo \"get HICKUP.M65 /tmp/monitor_load.HICKUP.M65\" | cadaver  \"https://app.scryptos.com/webdav/MEGA/groups/MEGA65%%20filehost/ShareFolder/Bitstreams/Jenkins-Out/mega65-core/issues/%d/%s/\"",issue,folder);
  fprintf(stderr,"%s\n",cmd);
  system(cmd);
  hyppo="/tmp/monitor_load.HICKUP.M65";
}

void load_bitstream(char *bitstream)
{
  if (vivado_bat!=NULL)	{
    /*  For Windows we just call Vivado to do the FPGA programming,
        while we are having horrible USB problems otherwise. */
    FILE *tclfile=fopen("temp.tcl","w");
    if (!tclfile) {
      fprintf(stderr,"ERROR: Could not create temp.tcl");
      exit(-1);
    }
    fprintf(tclfile,
        "open_hw\n" // "open_hw_manager\n"
        "connect_hw_server\n" // -allow_non_jtag\n"
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
        vivado_bat);
    printf("Running %s...\n",cmd);
    system(cmd);
    unlink("temp.tcl");
  } else {
    // No Vivado.bat, so try to use internal fpgajtag implementation.
    if (fpga_serial) {
      //fpgajtag_main(bitstream,fpga_serial);
    }
    else {
      //fpgajtag_main(bitstream,NULL);
    }
  }
  timestamp_msg("Bitstream loaded.\n");
}

void enter_hypervisor_mode(void)
{
  /* Ach! for some things we want to make sure we are in the hypervisor.
     This is a bit annoying, as we have to make sure we save state etc 
     properly.
     */
  monitor_sync();
  stop_cpu();
  slow_write_safe(fd,"sffd367e 0\r",11);
  slow_write_safe(fd,"\r",1);
  fprintf(stderr,"Foo!\n");
}

void return_from_hypervisor_mode(void)
{
  monitor_sync();
  slow_write_safe(fd,"sffd367f 0\r",11);
  monitor_sync();
  slow_write_safe(fd,"t0\r",3);
}


void open_the_serial_port(void)
{
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
}

int check_file_access(char *file,char *purpose)
{
  FILE *f=fopen(file,"rb");
  if (!f) {
    fprintf(stderr,"ERROR: Cannot access %s file '%s'\n",purpose,file);
    exit(-1);
  } else   fclose(f);

  return 0;
}

extern const char *version_string;

//int main(int argc,char **argv)
//{
//  start_time=time(0);
//
//  fprintf(stderr,"MEGA65 Cross-Platform tool.\n"
//	  "version: %s\n",version_string);
//  
//  timestamp_msg("");
//  fprintf(stderr,"Getting started..\n");
//
//  if (argc==1) usage();
//  
//  int opt;
//  while ((opt = getopt(argc, argv, "14B:b:c:C:d:EFHf:jJ:Kk:Ll:MnoprR:Ss:t:T:U:v:V:XZ:?")) != -1) {
//    switch (opt) {
//    case 'h': case '?': usage();
//    case 'X': hyppo_report=1; break;
//    case 'K': usedk=1; break;
//    case 'Z':
//      {
//	// Zap (reconfig) FPGA via MEGA65 reconfig registers
//	sscanf(optarg,"%x",&zap_addr);
//	fprintf(stderr,"Reconfiguring FPGA using bitstream at $%08x\n",zap_addr);
//	zap=1;
//      }
//      break;
//    case 'B': sscanf(optarg,"%x",&break_point); break;
//    case 'L': if (ethernet_video) { usage(); } else { ethernet_cpulog=1; } break;
//    case 'E': if (ethernet_cpulog) { usage(); } else { ethernet_video=1; } break;
//    case 'U': flashmenufile=strdup(optarg); check_file_access(optarg,"flash menu"); break;      
//    case 'R': romfile=strdup(optarg); check_file_access(optarg,"ROM"); break;      
//    case 'H': halt=1; break;
//    case 'C': charromfile=strdup(optarg); check_file_access(optarg,"char ROM"); break;      
//    case 'c': colourramfile=strdup(optarg); check_file_access(optarg,"colour RAM"); break;      
//    case '4': do_go64=1; break;
//    case '1': comma_eight_comma_one=1; break;
//    case 'p': pal_mode=1; break;
//    case 'n': ntsc_mode=1; break;
//    case 'F': reset_first=1; break; 
//    case 'r': do_run=1; break;
//    case 'f': fpga_serial=strdup(optarg); break;
//    case 'l': serial_port=strdup(optarg); break;
//    case 'M': mode_report=1; break;
//    case 'o': osk_enable=1; break;
//    case 'd': virtual_f011=1; d81file=strdup(optarg); break;
//    case 's':
//      serial_speed=atoi(optarg);
//      switch(serial_speed) {
//      case 1000000:
//      case 1500000:
//      case 4000000:
//      case 230400: case 2000000: break;
//      default: usage();
//      }
//      break;
//    case 'S':
//      screen_shot=1;
//      break;
//    case 'b':
//      bitstream=strdup(optarg);
//      if (bitstream[0]=='@') download_bitstream();
//      check_file_access(bitstream,"bitstream");
//      break;
//    case 'v':
//      vivado_bat=strdup(optarg);
//      break;
//    case 'j':
//      jtag_only=1; break;
//    case 'J':
//      boundary_scan=1;
//      sscanf(optarg,"%[^,],%[^,],%s",boundary_xdc,boundary_bsdl,jtag_sensitivity);
//      break;
//    case 'V':
//      set_vcd_file(optarg);
//      break;
//    case 'k':
//      hyppo=strdup(optarg);
//      if (hyppo[0]=='@') download_hyppo();
//      check_file_access(hyppo,"HYPPO");
//      break;
//    case 't': case 'T':
//      type_text=strdup(optarg);
//      if (opt=='T') type_text_cr=1;
//      break;
//    default: /* '?' */
//      usage();
//    }
//  }    
//  
//  // Automatically find the serial port on Linux, if one has not been
//  // provided
//  // Detect only A7100T parts
//  // XXX Will require patching for MEGA65 R1 PCBs, as they have an A200T part.
//#ifndef WINDOWS
//#ifdef __APPLE__
//  if (bitstream)
//#endif
//    init_fpgajtag(NULL, bitstream, 0x3631093); // 0xffffffff);
//#endif
//  
//#ifdef WINDOWS
//  if (boundary_scan) {
//    fprintf(stderr,"WARNING: JTAG boundary scan not implemented on Windows.\n");
//  }
//#else
//  if (boundary_scan) {
//    fprintf(stderr,"ERROR: threading on Windows not implemented.\n");
//    exit(-1);
//    // Launch boundary scan in a separate thread, so that we can monitor signals while
//    // running other operations.
//    if (pthread_create(&threads[thread_count++], NULL, run_boundary_scan, NULL))
//      perror("Failed to create JTAG boundary scan thread.\n");
//    else 
//      fprintf(stderr,"JTAG boundary scan launched in separate thread.\n");
//  }
//#endif
//
//  if (jtag_only) do_exit(0);
//  
//  if (argv[optind]) {
//    filename=strdup(argv[optind]);
//    check_file_access(filename,"programme");
//  }
//  
//  if (argc-optind>1) usage();
//  
//  // -b Load bitstream if file provided
//  if (bitstream) load_bitstream(bitstream);
//
//  if (virtual_f011) {
//    char msg[1024];
//    snprintf(msg,1024,"Remote access to disk image '%s' requested.\n",d81file);
//    timestamp_msg(msg);
//  }
//
//  open_the_serial_port();
//  
//  // XXX - Auto-detect if serial speed is not correct?
//  
//  //  printf("Calling monitor_sync()\n");
//  monitor_sync();
//
//  if (zap) {
//    char cmd[1024];
//    monitor_sync();
//    snprintf(cmd,1024,"sffd36c8 %x %x %x %x\r",
//	     (zap_addr>>0)&0xff,
//	     (zap_addr>>8)&0xff,
//	     (zap_addr>>16)&0xff,
//	     (zap_addr>>24)&0xff);
//    slow_write(fd,cmd,strlen(cmd));	  
//    monitor_sync();
//    mega65_poke(0xffd36cf,0x42);
//    fprintf(stderr,"FPGA reconfigure command issued.\n");
//    // XXX This can take a while, which we should accommodate
//    monitor_sync();
//  }  
//
//  if (hyppo_report) show_hyppo_report();
//
//  // If we have no HYPPO file provided, but need one, then
//  // extract one out of the running bitstream.
//  if (!hyppo) {
//    if (virtual_f011) {
//      timestamp_msg("Extracting HYPPO from running system...\n");
//      unsigned char hyppo_data[0x4000];
//      fetch_ram(0xFFF8000,0x4000,hyppo_data);
//      char *temp_name="/tmp/HYPPOEXT.M65";
//      FILE *f=fopen(temp_name,"wb");
//      if (!f) {
//	perror("Could not create temporary HYPPO file.");
//	exit(-1);
//      }
//      fwrite(hyppo_data,0x4000,1,f);
//      fclose(f);
//      hyppo=strdup(temp_name);
//    }
//  }
//  
//
//  if (!hyppo) {
//    
//    // XXX These two need the CPU to be in hypervisor mode
//    if (romfile||charromfile) {
//      enter_hypervisor_mode();
//      if (romfile) {
//	// Un-protect
//	mega65_poke(0xffd367d,mega65_peek(0xffd367d)&(0xff-4));
//		    
//	load_file(romfile,0x20000,0);
//	// reenable ROM write protect
//	mega65_poke(0xffd367d,mega65_peek(0xffd367d)|0x04);
//	
//      } 
//      if (charromfile) load_file(charromfile,0xFF7E000,0);
//      return_from_hypervisor_mode();
//    }
//    
//    if (colourramfile) load_file(colourramfile,0xFF80000,0);
//    if (flashmenufile) { load_file(flashmenufile,0x50000,0); } 
//  } else {
//    int patchKS=0;
//    if (romfile&&(!flashmenufile)) patchKS=1;
//
//    timestamp_msg("Replacing HYPPO...\n");
//    
//    stop_cpu();
//    if (hyppo) { load_file(hyppo,0xfff8000,patchKS); } 
//    if (flashmenufile) { load_file(flashmenufile,0x50000,0); } 
//    if (romfile) { load_file(romfile,0x20000,0); } 
//    if (charromfile) load_file(charromfile,0xFF7E000,0); 
//    if (colourramfile) load_file(colourramfile,0xFF80000,0); 
//    if (virtual_f011) {
//      timestamp_msg("Virtualising F011 FDC access.\n");
//
//      // Enable FDC virtualisation
//      mega65_poke(0xffd3659,0x01);
//      // Enable disk 0 (including for write)
//      mega65_poke(0xffd368b,0x03);
//    }
//    if (!reset_first) start_cpu();
//  }
//  
//  // -F reset
//  if (reset_first) { start_cpu(); slow_write(fd,"\r!\r",3); monitor_sync(); sleep(2); }
//
//  if (break_point!=-1) {
//    fprintf(stderr,"Setting CPU breakpoint at $%04x\n",break_point);
//    char cmd[1024];
//    sprintf(cmd,"b%x\r",break_point);
//    do_usleep(20000);
//    slow_write(fd,cmd,strlen(cmd));
//    do_exit(0);
//  }
//
//  if (pal_mode) { mega65_poke(0xffd306f,0); }
//  if (ntsc_mode) { mega65_poke(0xffd306f,0x80); }
//  if (ethernet_video) { mega65_poke(0xffd36e1,0x29); }
//  if (ethernet_cpulog) { mega65_poke(0xffd36e1,0x05); }
//
//  if (filename||do_go64) {
//    timestamp_msg("");
//    fprintf(stderr,"Detecting C64/C65 mode status.\n");
//    detect_mode();
//  }
//   
//  if (type_text) do_type_text(type_text);
//  
//  // -4 Switch to C64 mode
//  if ((!saw_c64_mode)&&do_go64) {
//    printf("Trying to switch to C64 mode...\n");
//    monitor_sync();
//    stuff_keybuffer("GO64\rY\r");    
//    saw_c65_mode=0;
//    do_usleep(100000);
//    detect_mode();
//    while (!saw_c64_mode) {
//      fprintf(stderr,"WARNING: Failed to switch to C64 mode.\n");
//      monitor_sync();
//      stuff_keybuffer("GO64\rY\r");    
//      do_usleep(100000);
//      detect_mode();
//    }
//  }
//
//  // Increase serial speed, if possible
//#ifndef WINDOWS
//  if (virtual_f011&&serial_speed==2000000) {
//    // Try bumping up to 4mbit
//    slow_write(fd,"\r+9\r",4);
//    set_serial_speed(fd,4000000);
//    serial_speed=4000000;
//  }
//#endif
//
//  // OSK enable
//  if (osk_enable) { mega65_poke(0xffd361f,0xff); printf("OSK Enabled\n"); }  
//
//  // -S screen shot
//  if (screen_shot) { stop_cpu(); do_screen_shot(); start_cpu(); exit(0); }
//
//  if (filename) {
//    timestamp_msg("");
//    fprintf(stderr,"Loading file '%s'\n",filename);
//
//    unsigned int load_routine_addr=0xf664;
//
//    int filename_matches=0;
//    int first_time=1;
//
//    // We REALLY need to know which mode we are in for LOAD
//    while (do_go64&&(!saw_c64_mode)) {
//      detect_mode();
//      if (!saw_c64_mode) {
//	fprintf(stderr,"ERROR: In C65 mode, but expected C64 mode\n");
//	exit(-1);
//      }
//    }
//    while ((!do_go64)&&(!saw_c65_mode)) {
//      detect_mode();
//      if (!saw_c65_mode) {
//	fprintf(stderr,"ERROR: Should be in C65 mode, but don't seem to be.\n");
//	exit(-1);
//      }
//    }
//    
//    while(!filename_matches) {    
//      
//      if (saw_c64_mode) load_routine_addr=0xf4a5;
//      // Type LOAD command and set breakpoint to catch the ROM routine
//      // when it executes.
//      breakpoint_set(load_routine_addr);
//      if (first_time) {	
//	if (saw_c64_mode) {
//	  // What we stuff in the keyboard buffer here is actually
//	  // not important for ,1 loading.  That gets handled in the loading
//	  // logic.  But we reflect it here, so that it doesn't confuse people.
//	  if (comma_eight_comma_one)
//	    stuff_keybuffer("Lo\"!\",8,1\r");
//	  else
//	    stuff_keybuffer("Lo\"!\",8\r");
//	}
//	else {
//	  // Really wait for C65 to get to READY prompt
//	  stuff_keybuffer("DLo\"!\r");
//	}
//      }
//      first_time=0;
//      breakpoint_wait();
//
//      int filename_addr=1;
//      unsigned char filename_len=mega65_peek(0xb7);
//      if (saw_c64_mode) filename_addr= mega65_peek(0xbb)+mega65_peek(0xbc)*256;
//      else {
//	filename_addr= mega65_peek(0xbb)+mega65_peek(0xbc)*256+mega65_peek(0xbe)*65536;
//      }
//      char requested_name[256];
//      fetch_ram(filename_addr,filename_len,(unsigned char *)requested_name);
//      requested_name[filename_len]=0;
//      timestamp_msg("");
//      fprintf(stderr,"Requested file is '%s' (len=%d)\n",requested_name,filename_len);
//      // If we caught the boot load request, then feed the DLOAD command again
//      if (!strcmp(requested_name,"0:AUTOBOOT.C65*")) first_time=1;
//
//      if (!strcmp(requested_name,"!")) break;
//      if (!strcmp(requested_name,"0:!")) break;
//
//      start_cpu();
//    }
//
//    // We can ignore the filename.
//    // Next we just load the file
//
//    int is_sid_tune=0;
//    
//    FILE *f=fopen(filename,"rb");
//    if (f==NULL) {
//      fprintf(stderr,"Could not find file '%s'\n",filename);
//      exit(-1);
//    } else {
//      char cmd[1024];
//      int load_addr=fgetc(f);
//      load_addr|=fgetc(f)<<8;
//      if ((load_addr==0x5350)||(load_addr==0x5352))
//	{
//	// It's probably a SID file
//
//	timestamp_msg("Examining SID file...\n");
//
//	// Read header
//	unsigned char sid_header[0x7c];
//	fread(sid_header,0x7c,1,f);
//	
//
//	unsigned int start_addr=(sid_header[0x0a-0x02]<<8)+sid_header[0x0b-0x02];
//	unsigned int play_addr=(sid_header[0x0c-0x02]<<8)+sid_header[0x0d-0x02];
//	unsigned int play_speed=sid_header[0x12-0x02];
//
//	char *name=&sid_header[0x16-0x02];
//	char *author=&sid_header[0x36-0x02];
//	char *released=&sid_header[0x56-0x02];
//
//	timestamp_msg("");
//	fprintf(stderr,"SID tune '%s' by '%s' (%s)\n",
//		name,author,released);
//
//	// Also show player info on the screen
//	char player_screen[1000]={
//	  "                                        "
//	  "                                        "
//	  "                                        "
//	  "M65 TOOL CRUSTY SID PLAYER V00.00 ALPHA "
//	  "                                        "
//	  "                                        "
//	  "                                        "
//	  "                                        "
//	  "                                        "
//	  "                                        "
//	  "                                        "
//	  "                                        "
//	  "                                        "
//	  "                                        "
//	  "0 - 9 = SELECT TRACK                    "
//	  "                                        "
//	  "                                        "
//	  "                                        "
//	  "                                        "
//	  "                                        "
//	  "                                        "
//	  "                                        "
//	  "                                        "
//	  "                                        "
//	  "                                        "
//	};
//	for(int i=0;name[i];i++) player_screen[40*6+i]=name[i];
//	for(int i=0;author[i];i++) player_screen[40*8+i]=author[i];
//	for(int i=0;released[i];i++) player_screen[40*10+i]=released[i];
//	
//	for(int i=0;i<1000;i++) {
//	  if (player_screen[i]>='@'&&player_screen[i]<='Z') player_screen[i]&=0x1f;
//	  if (player_screen[i]>='a'&&player_screen[i]<='z') player_screen[i]&=0x1f;
//	}
//
//	if (new_monitor) 
//	  sprintf(cmd,"l%x %x\r",0x0400,(0x0400+1000)&0xffff);
//	else
//	  sprintf(cmd,"l%x %x\r",0x0400-1,0x0400+1000-1);
//	slow_write(fd,cmd,strlen(cmd));
//	do_usleep(1000*SLOW_FACTOR);
//	{
//	  int n=1000;
//	  unsigned char *p=player_screen;
//	  while(n>0) {
//	    int w=serialport_write(fd,p,n);
//	    if (w>0) { p+=w; n-=w; } else do_usleep(1000*SLOW_FACTOR);
//	  }
//	}
//	do_usleep(50000);
//	
//	// Patch load address
//	load_addr=(sid_header[0x7d-0x02]<<8)+sid_header[0x7c-0x02];
//	timestamp_msg("");
//	fprintf(stderr,"SID load address is $%04x\n",load_addr);
//	//	dump_bytes(0,"sid header",sid_header,0x7c);
//	
//	// Prepare simple play routine
//	// XXX For now it is always VIC frame locked
//	timestamp_msg("Uploading play routine\n");
//	int b=56;
//	unsigned char player[56]={
//	  0x78,	  
//	  0xa9,0x35,
//	  0x85,0x01,
//	  0xa9,0x01,
//	  0x20,0x34,0x12,
//	  0xa9,0x80,0xcd,0x12,0xd0,0xd0,0xfb,0xa9,0x01,0x8d,0x20,0xd0,
//	  0x20,0x78,0x56,
//	  0xa9,0x00,0x8d,0x20,0xd0,0xa9,0x80,0xcd,0x12,0xd0,0xf0,0xfb,
//
//	  0xad,0x10,0xd6,
//	  0xf0,0x0b,
//	  0x8d,0x10,0xd6,
//	  0x29,0x0f,
//	  0x8d,0x21,0xd0,
//	  0x4c,0x07,0x04,
//	  
//	  0x4c,0x0A,0x04
//	};
//
//	player[6+0]=sid_header[0x11-0x02] - 1;
//	
//	if (start_addr) {
//	  player[8+0]=(start_addr>>0)&0xff;
//	  player[8+1]=(start_addr>>8)&0xff;
//	} else {
//	  player[7+0]=0xea;
//	  player[7+1]=0xea;
//	  player[7+2]=0xea;
//	}
//	if (play_addr) {
//	  player[23+0]=(play_addr>>0)&0xff;
//	  player[23+1]=(play_addr>>8)&0xff;
//	} else {
//	  player[22+0]=0xea;
//	  player[22+1]=0xea;
//	  player[22+2]=0xea;
//	}
//
//	// Enable M65 IO for keyboard scanning
//	slow_write(fd,"sffd302f 47\n",12);
//	slow_write(fd,"sffd302f 53\n",12);
//	
//	if (new_monitor) 
//	  sprintf(cmd,"l%x %x\r",0x0400,(0x0400+b)&0xffff);
//	else
//	  sprintf(cmd,"l%x %x\r",0x0400-1,0x0400+b-1);
//	slow_write(fd,cmd,strlen(cmd));
//	do_usleep(1000*SLOW_FACTOR);
//	int n=b;
//	unsigned char *p=player;
//	while(n>0) {
//	  int w=serialport_write(fd,p,n);
//	  if (w>0) { p+=w; n-=w; } else do_usleep(1000*SLOW_FACTOR);
//	}
//	
//	is_sid_tune=1;
//	
//      } else if (!comma_eight_comma_one) {
//	if (saw_c64_mode)
//	  load_addr=0x0801;
//	else
//	  load_addr=0x2001;
//	timestamp_msg("");
//	fprintf(stderr,"Forcing load address to $%04X\n",load_addr);
//      }
//      else printf("Load address is $%04x\n",load_addr);
//
//      
//      do_usleep(50000);
//      unsigned char buf[32768];
//      int max_bytes=32768;
//      int b=fread(buf,1,max_bytes,f);     
//      while(b>0) {
//	timestamp_msg("");
//	fprintf(stderr,"Read block for $%04x -- $%04x (%d bytes)\n",load_addr,load_addr+b-1,b);
//
//	if (is_sid_tune) {
//	  int num_sids=0;
//	  int sid_addrs[256];
//	  int fix_addrs[256];
//	  int this_sid=0;
//	  for(int i=0;i<b;i++) {
//	    switch (buf[i]) {
//	    case 0xD4: case 0xD5: case 0xD6: case 0xDE: case 0xDF:
//	      // Possible SID addresses
//	      // Check if opcode is an absolute load or store
//	      // If so, note the SID address, so we can reallocate any
//	      // that are out of range etc
//	      if (i>=2) {
//		// Look for absolute store instructions
//		switch(buf[i-2]) {
//		case 0x8D: //   STA $nnnn
//		case 0x99: //   STA $nnnn,Y
//		case 0x9D: //  STA $nnnn,X
//		case 0x8E: //  STX $nnnn
//		case 0x8C: //  STY $nnnn
//		  this_sid=buf[i]<<8; this_sid|=buf[i-1];
//		  this_sid&=0xffe0;
//		  int j=0;
//		  for(j=0;j<num_sids;j++)
//		    if (this_sid==sid_addrs[j]) break;
//		  if (j==num_sids) {
//		    sid_addrs[num_sids++]=this_sid;
//		    fprintf(stderr,"Tune uses SID at $%04x\n",this_sid);
//		  }
//		}
//	      }
//	      break;
//	    }
//	  }
//	  fprintf(stderr,"Tune uses a total of %d SIDs.\n",num_sids);
//	  for(int i=0;i<num_sids;i++) {
//	    if (sid_addrs[i]>=0xd600) {
//	      fix_addrs[i]=0xd400+0x20*i;
//	      fprintf(stderr,"Relocating SID at $%02x to $%04x\n",
//		      sid_addrs[i],fix_addrs[i]);
//	    } else fix_addrs[i]=sid_addrs[i];
//	    
//	  }
//	  for(int i=0;i<b;i++) {
//	    switch (buf[i]) {
//	    case 0xD4: case 0xD5: case 0xD6: case 0xDE: case 0xDF:
//	      // Possible SID addresses
//	      // Check if opcode is an absolute load or store
//	      // If so, note the SID address, so we can reallocate any
//	      // that are out of range etc
//	      if (i>=2) {
//		// Look for absolute store instructions
//		switch(buf[i-2]) {
//		case 0x8D: //   STA $nnnn
//		case 0x99: //   STA $nnnn,Y
//		case 0x9D: //  STA $nnnn,X
//		case 0x8E: //  STX $nnnn
//		case 0x8C: //  STY $nnnn
//		  this_sid=buf[i]<<8; this_sid|=buf[i-1];
//
//		  int j=0;
//		  for(j=0;j<num_sids;j++)
//		    if ((this_sid&0xffe0)==sid_addrs[j]) break;
//		  if (fix_addrs[j]!=sid_addrs[j]) {
//		    fprintf(stderr,"@ $%04X Patching $%04X to $%04X\n",
//			    i+load_addr,
//			    this_sid,fix_addrs[j]|(this_sid&0x1f));
//		    int fixed_addr=fix_addrs[j]|(this_sid&0x1f);
//		    buf[i-1]=fixed_addr&0xff;
//		    buf[i]=fixed_addr>>8;
//		  }
//		}
//	      }
//	      break;
//	    }
//	  }
//	}
//	
//#ifdef WINDOWS_GUS
//	// Windows doesn't seem to work with the l fast-load monitor command
//	printf("Asking Gus to write data...\n");
//	for(int i=0;i<b;i+=16) {
//	  int ofs=0;
//	  sprintf(cmd,"s%x",load_addr+i); ofs=strlen(cmd);
//	  for(int j=0;(j<16)&&(i+j)<b;j++) { sprintf(&cmd[ofs]," %x",buf[i+j]); ofs=strlen(cmd); }
//	  sprintf(&cmd[ofs],"\r"); ofs=strlen(cmd);
//	  slow_write(fd,cmd,strlen(cmd));
//	}
//#else	  
//	// load_addr=0x400;
//	if (new_monitor) 
//	  sprintf(cmd,"l%x %x\r",load_addr,(load_addr+b)&0xffff);
//	else
//	  sprintf(cmd,"l%x %x\r",load_addr-1,load_addr+b-1);
//	slow_write(fd,cmd,strlen(cmd));
//	do_usleep(1000*SLOW_FACTOR);
//	int n=b;
//	unsigned char *p=buf;
//	while(n>0) {
//	  int w=serialport_write(fd,p,n);
//	  if (w>0) { p+=w; n-=w; } else do_usleep(1000*SLOW_FACTOR);
//	}
//	if (serial_speed==230400) do_usleep(10000+50*b*SLOW_FACTOR);
//	  else if (serial_speed==2000000)
//	    // 2mbit/sec / 11bits/char (inc space) = ~5.5usec per char
//	    do_usleep(6*b*SLOW_FACTOR2);
//	  else
//	    // 4mbit/sec / 11bits/char (inc space) = ~2.6usec per char
//	    do_usleep(3*b*SLOW_FACTOR2);
//#endif
//	load_addr+=b;
//	b=fread(buf,1,max_bytes,f);	  
//      }
//      fclose(f); f=NULL;
//
//      // set end address, clear input buffer, release break point,
//      // jump to end of load routine, resume CPU at a CLC, RTS
//      monitor_sync();
//      
//      // Clear keyboard input buffer
//      if (saw_c64_mode) sprintf(cmd,"sc6 0\r");
//      else sprintf(cmd,"sd0 0\r");
//      slow_write(fd,cmd,strlen(cmd));
//      monitor_sync();
//
//      // Remove breakpoint
//      sprintf(cmd,"b\r");
//      slow_write(fd,cmd,strlen(cmd));
//      monitor_sync();
//
//      // We need to set X and Y to load address before
//      // returning: LDX #$ll / LDY #$yy / CLC / RTS
//      sprintf(cmd,"s380 a2 %x a0 %x 18 60\r",
//	      load_addr&0xff,(load_addr>>8)&0xff);
//      timestamp_msg("");
//      fprintf(stderr,"Returning top of load address = $%04X\n",load_addr);
//      slow_write(fd,cmd,strlen(cmd));
//      monitor_sync();
//
//      if ((!is_sid_tune)||(!do_run)) {
//	sprintf(cmd,"g0380\r");
//      } else
//	sprintf(cmd,"g0400\r");
//
//#if 1
//      slow_write(fd,cmd,strlen(cmd)); 
//      //      monitor_sync();
//      
//      if (!halt) {
//	start_cpu();
//      }
//      
//      if (do_run) {
//	stuff_keybuffer("RUN:\r");
//	timestamp_msg("RUNning.\n");
//      }
//#endif
//      
//      // loaded ok.
//      timestamp_msg("");
//      fprintf(stderr,"LOADED.\n");
//      
//    }
//  }
//  
//  // XXX - loop for virtualisation, JTAG boundary scanning etc
//  
//  do_exit(0);
//}

void do_exit(int retval) {
#ifndef WINDOWS
  timestamp_msg("");
  fprintf(stderr,"Background tasks may be running running. CONTROL+C to stop...\n");
  for(int i=0;i<thread_count;i++)
    ;//pthread_join(threads[i], NULL);     
#endif  
  exit(retval);
}
