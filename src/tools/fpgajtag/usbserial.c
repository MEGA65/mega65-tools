#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <libusb.h>

#ifdef __linux__
#include <dirent.h>
#include <linux/limits.h>
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/serial/IOSerialKeys.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <sys/param.h>
#include <IOKit/usb/IOUSBLib.h>
#endif // __APPLE__

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
  int score = 0;
  unsigned int len;
  uint16_t vendor_id, product_id;
  uint32_t location_id;
  uint8_t bus, pnum0, pnum1, pnum2, pnum3;

  bool found;
  CFMutableDictionaryRef classes_to_match;
  kern_return_t kern_result;
  io_iterator_t serial_port_iterator;
  io_service_t device;
  io_name_t name;
  io_struct_inband_t dev_node;
  io_registry_entry_t parent_registry_entry;
  UInt8 usb_interface_number;
  IOCFPlugInInterface **plug = NULL;
  IOUSBDeviceInterface **dev = NULL;
  IOUSBInterfaceInterface **intf = NULL;

  classes_to_match = IOServiceMatching(kIOSerialBSDServiceValue);
  if (classes_to_match == NULL) {
    log_debug("m65ser_get_candidates: IOServiceMatching returned a NULL dictionary.\n");
    return -1;
  }

  kern_result = IOServiceGetMatchingServices(kIOMasterPortDefault, classes_to_match, &serial_port_iterator);
  if (kern_result != KERN_SUCCESS) {
    log_debug("m65ser_get_candidates: IOServiceGetMatchingServices returned error %d\n", kern_result);
    return -1;
  }

  while ((device = IOIteratorNext(serial_port_iterator)) && usbdev_info_count < MAX_USBDEV_INFO) {
    len = 256;
    IORegistryEntryGetProperty(device, kIOCalloutDeviceKey, dev_node, &len);

    found = false;
    usb_interface_number = 255;

    for (;;) {
      // no more parents -> this is no usb device
      if (IORegistryEntryGetParentEntry(device, kIOServicePlane, &parent_registry_entry)) {
        break;
      }

      device = parent_registry_entry;
      kern_result = IOObjectGetClass(device, name);
      if (kern_result != KERN_SUCCESS) {
        break;
      }
      if (strcmp(name, "IOUserSerial") == 0) {
        kern_result = IORegistryEntryGetName(device, name);
        if (kern_result != KERN_SUCCESS) {
          break;
        }
        if (strcmp(name, "AppleUSBFTDI") != 0) {
          log_debug("skipping serial port %s with driver class %s\n", dev_node, name);
          break;
        }
      }

      if (IOObjectConformsTo(device, kIOUSBInterfaceClassName)) {
        kern_result = IOCreatePlugInInterfaceForService(
            device, kIOUSBInterfaceUserClientTypeID, kIOCFPlugInInterfaceID, &plug, &score);
        if (kern_result != KERN_SUCCESS || plug == NULL) {
          log_debug("m65ser_get_candidates: Can't obtain USB device plugin interface\n");
          continue;
        }
        kern_result = (*plug)->QueryInterface(plug, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID), (void *)&intf);
        if (kern_result != KERN_SUCCESS || intf == NULL) {
          log_debug("m65ser_get_candidates: Can't obtain USB interface plugin interface\n");
          IODestroyPlugInInterface(plug);
          continue;
        }
        IODestroyPlugInInterface(plug);

        if ((*intf)->GetInterfaceNumber(intf, &usb_interface_number) != kIOReturnSuccess) {
          log_debug("m65ser_get_candidates: Can't get usb interface number\n");
          (*intf)->Release(intf);
          continue;
        }
        (*intf)->Release(intf);
      }

      if (IOObjectConformsTo(device, kIOUSBDeviceClassName)) {
        found = true;
        break;
      }
    }

    if (!found)
      continue;

    kern_result = IOCreatePlugInInterfaceForService(
        device, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plug, &score);
    if (kern_result != KERN_SUCCESS || plug == NULL) {
      log_debug("m65ser_get_candidates: Can't obtain USB device plugin interface\n");
      continue;
    }

    kern_result = (*plug)->QueryInterface(plug, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID), (void *)&dev);
    IODestroyPlugInInterface(plug);
    if (kern_result != kIOReturnSuccess || dev == NULL) {
      log_debug("m65ser_get_candidates: Can't obtain USB interface\n");
      continue;
    }

    if ((*dev)->GetDeviceVendor(dev, &vendor_id) != kIOReturnSuccess
        || (*dev)->GetDeviceProduct(dev, &product_id) != kIOReturnSuccess
        || (*dev)->GetLocationID(dev, &location_id) != kIOReturnSuccess) {
      log_debug("m65ser_get_candidates: Can't read USB device parameters\n");
      (*dev)->Release(dev);
      continue;
    }
    (*dev)->Release(dev);

    bus = location_id >> 24;
    pnum0 = (location_id >> 20) & 0xf;
    pnum1 = (location_id >> 16) & 0xf;
    pnum2 = (location_id >> 12) & 0xf;
    pnum3 = (location_id >> 8) & 0xf;

    if (usb_interface_number != 1) {
      log_debug("skipping serial port %s (%d-%d.%d.%d.%d) with interface no %d\n", dev_node, bus, pnum0, pnum1, pnum2, pnum3,
          usb_interface_number);
    }
    else {
      log_info("detected serial port %s (%d-%d.%d.%d.%d)", dev_node, bus, pnum0, pnum1, pnum2, pnum3);
      usbdev_info[usbdev_info_count].device = strdup(dev_node);
      usbdev_info[usbdev_info_count].vendor_id = vendor_id;
      usbdev_info[usbdev_info_count].product_id = product_id;
      usbdev_info[usbdev_info_count].serial_no = NULL;
      usbdev_info[usbdev_info_count].bus = bus;
      usbdev_info[usbdev_info_count].pnum0 = pnum0;
      usbdev_info[usbdev_info_count].pnum1 = pnum1;
      usbdev_info[usbdev_info_count].pnum2 = pnum2;
      usbdev_info[usbdev_info_count].pnum3 = pnum3;
      usbdev_info_count++;
    }
  }

  IOObjectRelease(device);

#elif defined(__linux__)
  int bus;
  uint8_t pnum0 = 255, pnum1 = 255, pnum2 = 255, pnum3 = 255;
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
      // this could be done more elegantly...
      if (sscanf(pos, "/%d-%hhd.%hhd.%hhd.%hhd:", &bus, &pnum0, &pnum1, &pnum2, &pnum3) != 5) {
        if (sscanf(pos, "/%d-%hhd.%hhd.%hhd:", &bus, &pnum0, &pnum1, &pnum2) != 4) {
          if (sscanf(pos, "/%d-%hhd.%hhd:", &bus, &pnum0, &pnum1) != 3) {
            if (sscanf(pos, "/%d-%hhd:", &bus, &pnum0) != 2) {
              log_debug("m65ser_get_candidates: failed to parse bus/port (scan) %s", pos);
              continue;
            }
          }
        }
      }

      snprintf(link, PATH_MAX, "/dev/%s", de->d_name);

      if (pnum1 != 255 && pnum2 != 255 && pnum3 != 255)
        log_info("detected serial port %s (%d-%d.%d.%d.%d)", link, bus, pnum0, pnum1, pnum2, pnum3);
      else if (pnum1 != 255 && pnum2 != 255)
        log_info("detected serial port %s (%d-%d.%d.%d)", link, bus, pnum0, pnum1, pnum2);
      else if (pnum1 != 255)
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
      usbdev_info[usbdev_info_count].pnum2 = pnum2;
      usbdev_info[usbdev_info_count].pnum3 = pnum3;
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
          usbdev_info[usbdev_info_count].pnum2 = -1;
          usbdev_info[usbdev_info_count].pnum3 = -1;
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
