#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <libusb.h>

#ifndef WINDOWS
#include <glob.h>
#include <dirent.h>
#ifdef __linux__
#include <linux/limits.h>
#endif
#endif

#include <logging.h>
#include "usbserial.h"

int usbdev_info_index = -1;
int usbdev_info_count = 0;
usbdev_infoT usbdev_info[MAX_USBDEV_INFO];

/*
 * usbdev_get_candidates()
 *   returns number of devices found, negative for error
 *
 * builds a list of possible usb devices and stores them in
 * usbdev_info. Fields like serial_no, vendor_id and product_id
 * help to match the libusb device to it.
 * a maximum of 16 devices is supported (MAX_USBDEV_INFO)
 */
int usbdev_get_candidates(void)
{
#if defined(__APPLE__)
  // apple has no way (yet) to get information about usb devices
  int i;
  static char *devglob = "/dev/cu.usbserial-*";

  glob_t result;
  if (glob(devglob, 0, NULL, &result)) {
    globfree(&result);
    return -1;
  }

  for (i = 0; i < result.gl_pathc && usbdev_info_count < MAX_USBDEV_INFO; i++) {
    log_info("detected serial port %s (apple, no further information)", result.gl_pathv[i]);
    usbdev_info[usbdev_info_count].device = strdup(result.gl_pathv[i]);
    usbdev_info[usbdev_info_count].vendor_id = -1;
    usbdev_info[usbdev_info_count].product_id = -1;
    usbdev_info[usbdev_info_count].serial_no = NULL;
    usbdev_info[usbdev_info_count].bus = -1;
    usbdev_info[usbdev_info_count].pnum0 = -1;
    usbdev_info[usbdev_info_count].pnum1 = -1;
    usbdev_info_count++;
  }

  globfree(&result);
#elif defined(__linux__)
  int bus;
  uint8_t pnum0, pnum1;
  char path[PATH_MAX];
  char link[PATH_MAX];
  char *pos;
  DIR *d;
  struct dirent *de = NULL;

  // list linux usb devices
  if ((d = opendir("/sys/bus/usb-serial/devices"))) {
    while ((de = readdir(d)) != NULL && usbdev_info_count < MAX_USBDEV_INFO) {
      if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
        continue;

      snprintf(path, PATH_MAX, "/sys/bus/usb-serial/devices/%s", de->d_name);
      if (!realpath(path, link)) {
        log_debug("m65ser_get_candidates: fail to resolve path");
        continue; // could not resolve
      }

      // link is something like
      // ../../../devices/pci0000:00/0000:00:14.0/usb3/3-1/3-1.2/3-1.2:1.0/ttyUSB2
      // we need the 3-1.2 path, which is bus-pnum0.pnum1
      if (!(pos = strrchr(link, '/'))) {
        log_debug("m65ser_get_candidates: failed to parse bus/port (split1)");
        continue;
      }
      *pos = 0;
      if (!(pos = strrchr(link, '/'))) {
        log_debug("m65ser_get_candidates: failed to parse bus/port (split2)");
        continue;
      }
      if (sscanf(pos, "/%d-%hhd.%hhd[.:]", &bus, &pnum0, &pnum1) != 3) {
        if (sscanf(pos, "/%d-%hhd:", &bus, &pnum0) != 2) {
          log_debug("m65ser_get_candidates: failed to parse bus/port (scan) %s", pos);
          continue;
        } else
          pnum1 = 255;
      }

      snprintf(link, PATH_MAX, "/dev/%s", de->d_name);

      if (pnum1 != 255)
        log_info("detected serial port %s (%d-%d.%d)", link, bus, pnum0, pnum1);
      else
        log_info("detected serial port %s (%d-%d)", link, bus, pnum0);
      usbdev_info[usbdev_info_count].device = strdup(link);
      usbdev_info[usbdev_info_count].vendor_id = -1;
      usbdev_info[usbdev_info_count].product_id = -1;
      usbdev_info[usbdev_info_count].serial_no = NULL;
      usbdev_info[usbdev_info_count].bus = bus;
      usbdev_info[usbdev_info_count].pnum0 = pnum0;
      usbdev_info[usbdev_info_count].pnum1 = pnum1;
      usbdev_info_count++;
    }
  }
  else {
    log_debug("m65ser_get_candidates: failed to read /sys/bus/usb-serial/devices");
    return -1;
  }
#elif defined(WINDOWS)
  char *cmd = "powershell.exe -Command \"Get-CimInstance -ClassName Win32_PnPEntity\"";
  char *pos, dev_name[64], dev_serial[64], dev_vendor[64];
  int dev_vid, dev_pid, skip = 0;

  /*
  Output from powershell looks like this:
  Name                        : USB Serial Port (COM10)
  ...
  DeviceID                    : FTDIBUS\VID_0403+PID_6010+210292B17EA1A\0000
  ...
  Manufacturer                : FTDI
  [EmptyLine]
  [next entry]
  */

  char buf[256];
  FILE *fp;

  if ((fp = popen(cmd, "r")) == NULL) {
    log_debug("m65ser_get_candidates: popen powershell failed");
    return -1;
  }

  while (fgets(buf, 256, fp) != NULL) {
    if (skip) {
      if (buf[0] == '\r' || buf[0] == '\n')
        skip = 0;
      continue;
    }
    // Match COM port name
    if (!strncmp(buf, "Name", 4)) {
      if ((pos = strstr(buf, "COM"))) {
        strncpy(dev_name, pos, 63);
        dev_name[63] = 0;
        if ((pos = strchr(dev_name, ')'))) {
          *pos = 0;
          // log_debug("m65ser_get_candidates: name is %s", dev_name);
          continue;
        }
      }
      dev_name[0] = 0;
      // don't spam the log with non COM entries
      // log_debug("detect_win_serial: failed to parse name '%s'", buf);
      skip = 1;
    }
    else if (!strncmp(buf, "DeviceID", 8)) {
      if ((pos = strstr(buf, "VID"))) {
        if (sscanf(pos, "VID_%X+PID_%X+%63s\\", &dev_vid, &dev_pid, (char *)&dev_serial) == 3) {
          if ((pos = strstr(dev_serial, "\\00"))) {
            *pos = 0;
          }
          // log_debug("m65ser_get_candidates: vid=%04x pid=%04x serial=%s", dev_vid, dev_pid, dev_serial);
          continue;
        }
      }
      log_debug("m65ser_get_candidates: failed to parse deviceid '%s'", buf);
      skip = 1;
    }
    else if (!strncmp(buf, "Manufacturer", 12)) {
      if ((pos = strstr(buf, ": "))) {
        strncpy(dev_vendor, pos + 2, 63);
        dev_vendor[63] = 0;
        if ((pos = strstr(dev_vendor, "\n")))
          *pos = 0;

        // log_debug("m65ser_get_candidates: vendor is %s", dev_vendor);
        log_info("detected %s serial port %s (%04X/%04X, serial %s)", dev_vendor, dev_name, dev_vid, dev_pid, dev_serial);

        if (usbdev_info_count < MAX_USBDEV_INFO) {
          usbdev_info[usbdev_info_count].device = strdup(dev_name);
          usbdev_info[usbdev_info_count].vendor_id = dev_vid;
          usbdev_info[usbdev_info_count].product_id = dev_pid;
          usbdev_info[usbdev_info_count].serial_no = strdup(dev_serial);
          usbdev_info[usbdev_info_count].bus = -1;
          usbdev_info[usbdev_info_count].pnum0 = -1;
          usbdev_info[usbdev_info_count].pnum1 = -1;
          usbdev_info_count++;
        }
        else
          log_warn("max usb devices reached");

        continue;
      }
      log_debug("m65ser_get_candidates: failed to parse vendor '%s'", buf);
      skip = 1;
    }
  }

  if (pclose(fp)) {
    log_debug("m65ser_get_candidates: powershell not found or exited with error status");
    return -1;
  }
#endif

  return usbdev_info_count;
}

char *usbdev_get_next_device(const int start)
{
  if (start) {
    usbdev_info_index = -1;
    return NULL;
  }

  usbdev_info_index++;
  if (usbdev_info_index < 0 || usbdev_info_index >= usbdev_info_count)
    return NULL;

  return usbdev_info[usbdev_info_index].device;
}
