#include <stdio.h>
#include <string.h>

#include <hal.h>
#include <memory.h>
#include <dirent.h>
#include <fileio.h>

//#define DEBUG_BITBASH(x) { printf("@%d:%02x",__LINE__,x); }
#define DEBUG_BITBASH(x)

char *select_bitstream_file(void);
void fetch_rdid(void);
void flash_reset(void);

unsigned char joy_x = 100;
unsigned char joy_y = 100;

unsigned char latency_code = 0xff;
unsigned char reg_cr1 = 0x00;
unsigned char reg_sr1 = 0x00;

unsigned char manufacturer;
unsigned short device_id;
unsigned short cfi_data[512];
unsigned short cfi_length = 0;

unsigned char reconfig_disabled = 0;

unsigned char data_buffer[512];
// Magic string for identifying properly loaded bitstream
unsigned char bitstream_magic[16] =
    // "MEGA65BITSTREAM0";
    { 0x4d, 0x45, 0x47, 0x41, 0x36, 0x35, 0x42, 0x49, 0x54, 0x53, 0x54, 0x52, 0x45, 0x41, 0x4d, 0x30 };

unsigned short mb = 0;

unsigned char buffer[512];

short i, x, y, z;
short a1, a2, a3;
unsigned char n = 0;

void progress_bar(unsigned char onesixtieths);
void read_data(unsigned long start_address);
void program_page(unsigned long start_address);
void erase_sector(unsigned long address_in_sector);

void wait_10ms(void)
{
  // 16 x ~64usec raster lines = ~1ms
  int c = 160;
  unsigned char b;
  while (c--) {
    b = PEEK(0xD012U);
    while (b == PEEK(0xD012U))
      continue;
  }
}

/*
  The NOR flash on the MEGAphone is purposely on a slow indirect I2C IO expander connection,
  so that any pad material can't be read out fast, so that an attacker will require a long time
  to read much pad data.

  The pins we have are:
  SCK        - OUT - Clock, and the only directly connected pin. Plumbed as though it were the IEC bus CLK line
  HOLD/IO3   - OUT - IO Expander 2, Port 1, bit 1
  /RESET     - OUT - IO Expander 2, Port 1, bit 2
  /CS2       - OUT - IO Expander 2, Port 1, bit 3
  /CS1       - OUT - IO Expander 2, Port 1, bit 4
  /WP / IO2  - OUT - IO Expander 2, Port 1, bit 5
  SI/IO0     - OUT - IO Expander 2, Port 0, bit 6
  SO/IO1     - IN  - IO Expander 2, Port 0, bit 5

  So we set port 1 to be all output, and bit 5 of port 0 to be input.

  I2C busy status can be read from $FFD70FF. If non-zero, wait before writing.
  When reading, we have to also allow enough time for a complete read loop to complete.
  This is quite slow. We should probably have a register we can read to check this.

  IO expanders have 8 registers:

  $FFD7008 - Port 0 read
  $FFD7009 - Port 1 read
  $FFD700A - Port 0 write
  $FFD700B - Port 1 write
  $FFD700C - Port 0 polarity invert
  $FFD700D - Port 1 polarity invert
  $FFD700E - Port 0 DDR (1s = input pins)
  $FFD700F - Port 1 DDR (1s = input pins)

*/

unsigned char port1 = 0xff;
unsigned char port0 = 0xff;

unsigned char ev = 0;

void iowrite(unsigned char reg, unsigned char val)
{
  while (lpeek(0xFFD70FF))
    continue;
  lpoke(0xFFD7008 + reg, val);
  // Make sure write completes before returning, so that data changes before clock etc
  while (!lpeek(0xFFD70FF))
    continue;
  while (lpeek(0xFFD70FF))
    continue;
}

void setup_i2c_ports(void)
{
  // Set bits high
  iowrite(2, 0xff);
  port0 = 0xff;
  iowrite(3, 0xff);
  port1 = 0xff;
  // Reset polarity to normal
  iowrite(4, 0x00);
  iowrite(5, 0x00);
  // Set bit 5 or port 0 to input
  iowrite(6, 0x20);
  // Set port 1 to input
  iowrite(7, 0x00);
  // Float CLK
  POKE(0xDD00, PEEK(0xDD00) & 0xEF);
}

void update_port_bits(void)
{
  iowrite(3, port1);
}

unsigned int di;
void delay(void)
{
  // Slow down signalling for easier debug

  //   for(di=0;di<1000;di++) continue;
}

void spi_tristate_so(void)
{
  // This is already done
}

unsigned char spi_sample_so(void)
{
  // Wait 1 complete read cycles to be sure data is fresh
  unsigned char v = lpeek(0xFFD70FE);
  while (lpeek(0xFFD70FE) == v)
    continue;
  v = lpeek(0xFFD70FE);
  while (lpeek(0xFFD70FE) == v)
    continue;

  v = lpeek(0xFFD7008) & 0x20;
  //  printf("%02x sample SO = %d\n",ev++,v?1:0);
  if (v)
    return 1;
  else
    return 0;
}

void spi_si_set(unsigned char b)
{
  // De-tri-state SO data line, and set value
  if (b)
    port0 |= 0x40;
  else
    port0 &= 0xbf;
  iowrite(2, port0);
  //  printf("%02x set SI = %d\n",ev++,b?1:0);
}

void spi_clock_low(void)
{
  // Activate IEC CLK line
  POKE(0xDD00, PEEK(0xDD00) | 0x10);
  wait_10ms();
  //  printf("%02x CLK low\n",ev++);
}

void spi_clock_high(void)
{
  POKE(0xDD00, PEEK(0xDD00) & 0xEF);
  wait_10ms();
  //  printf("%02x CLK high\n",ev++);
}

void spi_idle_clocks(unsigned int count)
{
  while (count--) {
    spi_clock_low();
    delay();
    spi_clock_high();
    delay();
  }
}

void spi_cs_low(void)
{
  port1 &= 0xEF;
  iowrite(3, port1);
  printf("CS low\n");
}

void spi_cs_high(void)
{
  port1 |= 0x10;
  iowrite(3, port1);
  printf("CS high\n");
}

void spi_tx_bit(unsigned char bit)
{
  spi_clock_low();
  spi_si_set(bit);
  delay();
  spi_clock_high();
  delay();
}

void spi_tx_byte(unsigned char b)
{
  unsigned char i;
  printf("%02x TX byte $%02x\n", ev++, b);
  for (i = 0; i < 8; i++) {
    spi_tx_bit(b & 0x80);
    b = b << 1;
  }
}

unsigned char spi_rx_byte()
{
  unsigned char b = 0;
  unsigned char i;

  b = 0;

  spi_tristate_so();
  for (i = 0; i < 8; i++) {
    spi_clock_low();
    b = b << 1;
    delay();
    if (spi_sample_so())
      b |= 0x01;
    spi_clock_high();
    delay();
  }

  return b;
}

void flash_reset(void)
{
  spi_cs_high();
  spi_clock_high();
  delay();
  spi_cs_low();
  delay();
  spi_tx_byte(0x60);
  spi_cs_high();
  usleep(32000);
  usleep(32000);
  usleep(32000);
  usleep(32000);
  usleep(32000);
  usleep(32000);
}

void read_registers(void)
{

  // Status Register 1 (SR1)
  spi_cs_high();
  spi_clock_high();
  delay();
  spi_cs_low();
  delay();
  spi_tx_byte(0x05);
  reg_sr1 = spi_rx_byte();
  spi_cs_high();
  delay();

  // Config Register 1 (CR1)
  spi_cs_high();
  spi_clock_high();
  delay();
  spi_cs_low();
  delay();
  spi_tx_byte(0x35);
  reg_cr1 = spi_rx_byte();
  spi_cs_high();
  delay();
}

void spi_write_enable(void)
{
  while (!(reg_sr1 & 0x02)) {
    spi_cs_high();
    spi_clock_high();
    delay();
    spi_cs_low();
    delay();
    spi_tx_byte(0x06);
    spi_cs_high();

    read_registers();
  }
}

void erase_sector(unsigned long address_in_sector)
{

  // XXX Send Write Enable command (0x06 ?)
  //  printf("activating write enable...\n");
  spi_write_enable();

  // XXX Clear status register (0x30)
  //  printf("clearing status register...\n");
  while (reg_sr1 & 0x61) {
    spi_cs_high();
    spi_clock_high();
    delay();
    spi_cs_low();
    delay();
    spi_tx_byte(0x30);
    spi_cs_high();

    read_registers();
  }

  // XXX Erase 64/256kb (0xdc ?)
  // XXX Erase 4kb sector (0x21 ?)
  //  printf("erasing sector...\n");
  spi_cs_high();
  spi_clock_high();
  delay();
  spi_cs_low();
  delay();
  spi_tx_byte(0xdc);
  spi_tx_byte(address_in_sector >> 24);
  spi_tx_byte(address_in_sector >> 16);
  spi_tx_byte(address_in_sector >> 8);
  spi_tx_byte(address_in_sector >> 0);
  spi_cs_high();

  while (reg_sr1 & 0x03) {
    read_registers();
  }

  if (reg_sr1 & 0x20)
    printf("error erasing sector @ $%08x\n", address_in_sector);
  else {
    printf("sector at $%08llx erased.\n%c", address_in_sector, 0x91);
  }
}

unsigned char first, last;

void fetch_rdid(void)
{
  /* Run command 0x9F and fetch CFI etc data.
     (Section 9.2.2)
   */

  unsigned short i;

  spi_cs_high();
  spi_clock_high();
  delay();
  spi_cs_low();
  delay();

  spi_tx_byte(0x9f);

  // Data format according to section 11.2

  // Start with 3 byte manufacturer + device ID
  manufacturer = spi_rx_byte();
  device_id = spi_rx_byte() << 8;
  device_id |= spi_rx_byte();

  printf("Manufacturer = %02x, devID=%02x\n", manufacturer, device_id);
  while (!PEEK(0xD610)) {
    continue;
  }
  POKE(0xD610, 0);

  // Now get the CFI data block
  for (i = 0; i < 512; i++)
    cfi_data[i] = 0x00;
  cfi_length = spi_rx_byte();
  if (cfi_length == 0)
    cfi_length = 512;
  for (i = 0; i < cfi_length; i++)
    cfi_data[i] = spi_rx_byte();

  spi_cs_high();
  delay();
  spi_clock_high();
  delay();
}

unsigned long addr;

unsigned int base_addr;

void main(void)
{
  unsigned char valid;
  unsigned char selected = 0;

  mega65_io_enable();

  // Disable OSK
  lpoke(0xFFD3615L, 0x7F);

  // Enable VIC-III attributes
  POKE(0xD031, 0x20);

  // Setup IO expanders for NOR flash access
  setup_i2c_ports();

  // Let CS lines float high
  spi_cs_high();
  delay();

  fetch_rdid();
  read_registers();
  if ((manufacturer == 0xff) && (device_id == 0xffff)) {
    printf("ERROR: Cannot communicate with QSPI            flash device.\n");
    return;
  }
#if 1
  printf("qspi flash manufacturer = $%02x\n", manufacturer);
  printf("qspi device id = $%04x\n", device_id);
  printf("rdid byte count = %d\n", cfi_length);
  printf("sector architecture is ");
  if (cfi_data[4 - 4] == 0x00)
    printf("uniform 256kb sectors.\n");
  else if (cfi_data[4 - 4] == 0x01)
    printf("4kb parameter sectors with 64kb sectors.\n");
  else
    printf("unknown ($%02x).\n", cfi_data[4 - 4]);
  printf("part family is %02x-%c%c\n", cfi_data[5 - 4], cfi_data[6 - 4], cfi_data[7 - 4]);
  printf("2^%d byte page, program typical time is 2^%d microseconds.\n", cfi_data[0x2a - 4], cfi_data[0x20 - 4]);
  printf("erase typical time is 2^%d milliseconds.\n", cfi_data[0x21 - 4]);
#endif

  // Work out size of flash in MB
  {
    unsigned char n = cfi_data[0x27 - 4];
    mb = 1;
    n -= 20;
    while (n) {
      mb = mb << 1;
      n--;
    }
  }
#if 1
  printf("flash size is %dmb.\n", mb);
#endif

  latency_code = reg_cr1 >> 6;
#if 1
  printf("latency code = %d\n", latency_code);
  if (reg_sr1 & 0x80)
    printf("flash is write protected.\n");
  if (reg_sr1 & 0x40)
    printf("programming error occurred.\n");
  if (reg_sr1 & 0x20)
    printf("erase error occurred.\n");
  if (reg_sr1 & 0x02)
    printf("write latch enabled.\n");
  else
    printf("write latch not (yet) enabled.\n");
  if (reg_sr1 & 0x01)
    printf("device busy.\n");
#endif

  printf("Press any key to continue...\n");
  while (PEEK(0xD610))
    POKE(0xD610, 0);
  while (!PEEK(0xD610))
    continue;
  while (PEEK(0xD610))
    POKE(0xD610, 0);

#if 0

  for(addr=0x400000L;addr<0x800000L;addr+=512) {
    read_data(addr);

    printf("Data read from flash is:\n");
    for(i=0;i<512;i+=64) {
      for(x=0;x<64;x++) {
	if (!(x&7)) printf("%08llx : ",addr+i+x);
	printf(" %02x",data_buffer[i+x]);
	if ((x&7)==7) printf("\n");
      }
    
      printf("Press any key to continue...\n");
      while(PEEK(0xD610)) POKE(0xD610,0);
      while(!PEEK(0xD610)) continue;
      while(PEEK(0xD610)) POKE(0xD610,0);
    }
  }

  while(1) continue;
#endif
}
