/*
The MIT License (MIT)

Copyright (c) 2014 Tod E. Kurt

Modified 2020 Paul Gardner-Stephen for MEGA65 monitor_load integration

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/*
 * listComPorts.c -- list COM ports
 *
 * http://github.com/todbot/usbSearch/
 *
 * 2012, Tod E. Kurt, http://todbot.com/blog/
 *
 *
 * Uses DispHealper : http://disphelper.sourceforge.net/
 *
 * Notable VIDs & PIDs combos:
 * VID 0403 - FTDI
 *
 * VID 0403 / PID 6001 - Arduino Diecimila
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

extern char *serialport;

#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidclass.h>

#include "disphelper.h"

int listComPorts(void)
{
  printf("Searching for COM ports...\n");

  DISPATCH_OBJ(wmiSvc);
  DISPATCH_OBJ(colDevices);

  dhInitialize(TRUE);
  dhToggleExceptions(TRUE);

  dhGetObject(L"winmgmts:{impersonationLevel=impersonate}!\\\\.\\root\\cimv2",
      // dhGetObject(L"winmgmts:\\\\.\\root\\cimv2",
      NULL, &wmiSvc);
  dhGetValue(L"%o", &colDevices, wmiSvc, L".ExecQuery(%S)", L"Select * from Win32_PnPEntity");

  int port_count = 0;

  FOR_EACH(objDevice, colDevices, NULL)
  {

    char *name = NULL;
    char *pnpid = NULL;
    char *manu = NULL;
    char *match;

    dhGetValue(L"%s", &name, objDevice, L".Name");
    dhGetValue(L"%s", &pnpid, objDevice, L".PnPDeviceID");

    if (name != NULL && ((match = strstr(name, "(COM")) != NULL)) { // look for "(COM23)"
      // 'Manufacturuer' can be null, so only get it if we need it
      dhGetValue(L"%s", &manu, objDevice, L".Manufacturer");
      port_count++;
      char *comname = strtok(match, "()");

      if (!strcmp(manu, "FTDI")) {
        if (!serialport) {
          fprintf(stderr, "Found FTDI USB serial adapter on %s\n", comname);
          serialport = strdup(comname);
        }
      }

      //            printf("%s - %s - %s\n",comname, manu, pnpid);
      dhFreeString(manu);
    }

    dhFreeString(name);
    dhFreeString(pnpid);
  }
  NEXT(objDevice);

  SAFE_RELEASE(colDevices);
  SAFE_RELEASE(wmiSvc);

  dhUninitialize(TRUE);

  return 0;
}
