#ifndef FPGAJTAG_H
#define FPGAJTAG_H

/*
 * flags in util.c
 */
// if this is !=0 after init_fpgajtag, some usb devices could not been opened
// could be a unix access problem
extern int fpgajtag_libusb_open_failed;

// enable USBDK interface
extern int fpgajtag_usbdk_enable;

/*
 * init_fpgajtag(serialno, serialport, fpga_id)
 *   returns usb device string
 *
 * this will probe the usb interfaces and decide which one to use,
 * depending on the serialno, serialport and fpga_id given.
 *
 */
char *init_fpgajtag(const char *serialno, const char *serialport, const uint32_t fpga_id);

/*
 * fpgajtag_main(bitstream)
 *
 * pushes bistream via JTAG to FPGA
 * JTAG must have been initialised using init_fpgajtag prior to this!
 */
int fpgajtag_main(char *bitstream);

// boundary scan stuff, don't know
int xilinx_boundaryscan(char *xdc, char *bsdl, char *sensitivity);
void set_vcd_file(char *name);

/*
 * usbdev_get_next_device(const int start)
 *   returns pointer to device string
 *
 * if start is 1, the iteration is reset and NULL is returned.
 * Otherwise return next device. NULL signifies the end of the list.
 * Requires init_fpgajtag to be ran first!
 */
char *usbdev_get_next_device(const int start);

#endif /* FPGAJTAG_H */