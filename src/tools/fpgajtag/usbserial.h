#ifndef USBSERIAL_H
#define USBSERIAL_H

#include <stdint.h>

#define MAX_USBDEV_INFO 16

typedef struct {
  char *device;
  // windows
  char *serial_no;
  int vendor_id, product_id;
  // linux
  int bus;
  uint8_t pnum0, pnum1;
} usbdev_infoT;

extern int usbdev_info_count;
extern usbdev_infoT usbdev_info[MAX_USBDEV_INFO];

int usbdev_get_candidates(void);

#endif /* USBSERIAL_H */