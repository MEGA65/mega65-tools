int vfdc_track, vfdc_sector, vfdc_side;
a int process_char(unsigned char c, int live)
{
  // printf("char $%02x\n",c);

  // Remember recent chars for virtual FDC access, as the Hypervisor tells
  // us which track, sector and side, before it sends the marker
  if (c == '!' && virtual_f011) {
#ifdef WINDOWS
    printf("[T+%I64dsec] : V-FDC read request from UART monitor: Track:%d, Sector:%d, Side:%d.\n", time(0) - start_time,
        vfdc_track, vfdc_sector, vfdc_side);
#else
    printf("[T+%ldsec] : V-FDC read request from UART monitor: Track:%d, Sector:%d, Side:%d.\n", time(0) - start_time,
        vfdc_track, vfdc_sector, vfdc_side);
#endif
    // We have all we need, so just read the sector from disk, upload it, and mark the job done
    virtual_f011_read(0, vfdc_track, vfdc_sector, vfdc_side);
  }
  if (c == '\\' && virtual_f011) {
#ifdef WINDOWS
    printf("[T+%I64dsec] : V-FDC write request from UART monitor: Track:%d, Sector:%d, Side:%d.\n", time(0) - start_time,
        vfdc_track, vfdc_sector, vfdc_side);
#else
    printf("[T+%ldsec] : V-FDC write request from UART monitor: Track:%d, Sector:%d, Side:%d.\n", time(0) - start_time,
        vfdc_track, vfdc_sector, vfdc_side);
#endif
    // We have all we need, so just read the sector from disk, upload it, and mark the job done
    sdbuf_request_addr = WRITE_SECTOR_BUFFER_ADDRESS;
    {
      char cmd[1024];
      sprintf(cmd, "M%x\r", sdbuf_request_addr);
      printf("Requesting reading of sector buffer: %s", cmd);
      a slow_write(fd, cmd, strlen(cmd));
    }
    saved_side = vfdc_side & 0x3f;
    saved_track = vfdc_track;
    saved_sector = vfdc_sector;
  }
  vfdc_track = vfdc_sector;
  vfdc_sector = vfdc_side;
  vfdc_side = c & 0x7f;

  if (c == '\r' || c == '\n') {
    line[line_len] = 0;
    if (line_len > 0)
      process_line(line, live);
    line_len = 0;
  }
  else {
    if (line_len < 1023)
      line[line_len++] = c;
  }
  return 0;
}

int process_waiting(PORT_TYPE fd)
{
  unsigned char read_buff[1024];
  int b = serialport_read(fd, read_buff, 1024);
  while (b > 0) {
    int i;
    for (i = 0; i < b; i++) {
      process_char(read_buff[i], 1);
    }
    b = serialport_read(fd, read_buff, 1024);
  }
  return 0;
}

int process_line(char* line, int live)
{
  int pc, a, x, y, sp, p;
  //   printf("[%s]\n",line);
  if (!live)
    return 0;
  if (strstr(line, "ws h RECA8LHC")) {
    if (!new_monitor)
      printf("Detected new-style UART monitor.\n");
    new_monitor = 1;
  }
  if (sscanf(line, "%04x %02x %02x %02x %02x %02x", &pc, &a, &x, &y, &sp, &p) == 6) {
    printf("PC=$%04x\n", pc);
    if (pc == 0xf4a5 || pc == 0xf4a2 || pc == 0xf666) {
      // Intercepted LOAD command
      printf("LOAD vector intercepted\n");
      stop_cpu();
      state = 1;
    }
    else if ( //  (pc>=0x8000&&pc<0xc000)&&
        (hyppo)) {
    }
    else {
      if (state == 99) {
        // Synchronised with monitor
        state = 0;
        // Send ^U r <return> to print registers and get into a known state.
        do_usleep(50000);
        slow_write(fd, "\r", 1);
        if (!halt) {
          start_cpu();
        }
        do_usleep(20000);
        printf("Synchronised with monitor.\n");
      }
    }
    if (sscanf(line, " :00000B7 %02x %*02x %*02x %*02x %02x %02x", &name_len, &name_lo, &name_hi) == 3) {
      if (not_already_loaded || name_len > 1) {
        name_addr = (name_hi << 8) + name_lo;
        printf("Filename is %d bytes long, and is stored at $%04x\n", name_len, name_addr);
        char filename[16];
        snprintf(filename, 16, "m%04x\r", name_addr);
        do_usleep(10000);
        slow_write(fd, filename, strlen(filename));
        printf("Asking for filename from memory: %s\n", filename);
        state = 3;
      }
    }
    if (sscanf(line, ":000000B7:%08x%08x", &name_len, &name_addr) == 2) {
      if (not_already_loaded) {
        name_len = name_len >> 24;
        printf("Filename is %d bytes long, from 0x%08x\n", name_len, name_addr);
        name_addr = (name_addr >> 24) + ((name_addr >> 8) & 0xff00);
        printf("Filename is %d bytes long, and is stored at $%04x\n", name_len, name_addr);
        char filename[16];
        snprintf(filename, 16, "m%04x\r", name_addr);
        do_usleep(10000);
        slow_write(fd, filename, strlen(filename));
        printf("Asking for filename from memory: %s\n", filename);
        state = 3;
      }
    }
    {
      int addr;
      int b[16];
      int gotIt = 0;
      unsigned int v[4];
      if (line[0] == '?')
        fprintf(stderr, "%s\n", line);
      if (sscanf(line, ":%x:%08x%08x%08x%08x", &addr, &v[0], &v[1], &v[2], &v[3]) == 5) {
        for (int i = 0; i < 16; i++)
          b[i] = (v[i / 4] >> ((3 - (i & 3)) * 8)) & 0xff;
        gotIt = 1;
      }
      if (sscanf(line, " :%x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", &addr, &b[0],
              &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7], &b[8], &b[9], &b[10], &b[11], &b[12], &b[13], &b[14], &b[15])
          == 17)
        gotIt = 1;
      if (gotIt) {
        char fname[17];
        //      if (!screen_shot) printf("Read memory @ $%04x\n",addr);
        if (addr == name_addr) {
          for (int i = 0; i < 16; i++) {
            fname[i] = b[i];
          }
          fname[16] = 0;
          fname[name_len] = 0;
          printf("Request to load '%s'\n", fname);
          if (fname[0] == '!' || (!strcmp(fname, "0:!"))) {
            // we use this form in case junk gets typed while we are doing it
            if (not_already_loaded)
              state = 2; // load specified file
            not_already_loaded = 0;
            // and change filename, so that we don't get stuck in a loop repeatedly loading
            char cmd[1024];
            snprintf(cmd, 1024, "s%x 41\r", name_addr);
            fprintf(stderr, "Replacing filename: %s\n", cmd);
            slow_write(fd, cmd, strlen(cmd));
          }
          else {
            printf("Specific file to load is '%s'\n", fname);
            if (filename)
              free(filename);
            filename = strdup(fname);
            do_go64 = 1; // load in C64 mode only
            state = 0;
          }
        }
        else if (addr == sdbuf_request_addr) {
          printf("Saw data for write buffer @ $%x\n", addr);

          int i;
          for (i = 0; i < 16; i++)
            sd_sector_buf[sdbuf_request_addr - WRITE_SECTOR_BUFFER_ADDRESS + i] = b[i];
          sdbuf_request_addr += 16;

          if (sdbuf_request_addr == (WRITE_SECTOR_BUFFER_ADDRESS + 0x100)) {
            // Request next $100 of buffer
            char cmd[1024];
            sprintf(cmd, "M%x\r", sdbuf_request_addr);
            printf("Requesting reading of second half of sector buffer: %s", cmd);
            slow_write(fd, cmd, strlen(cmd));
          }

          if (sdbuf_request_addr == (WRITE_SECTOR_BUFFER_ADDRESS + 0x200)) {

            dump_bytes(0, "Sector to write", sd_sector_buf, 512);

            char cmd[1024];

            int physical_sector = (saved_side == 0 ? saved_sector - 1 : saved_sector + 9);
            int result = fseek(fd81, (saved_track * 20 + physical_sector) * 512, SEEK_SET);
            if (result) {

              fprintf(stderr, "Error finding D81 sector %d %d\n", result, (saved_track * 20 + physical_sector) * 512);
              exit(-2);
            }
            else {
              int b = -1;
              b = fwrite(sd_sector_buf, 1, 512, fd81);
              if (b != 512) {

                fprintf(stderr, "Could not write D81 file: '%s'\n", d81file);
                exit(-1);
              }
              fprintf(stderr, "write: %d @ 0x%x\n", b, (saved_track * 20 + physical_sector) * 512);
            }

            // block loaded save it now
            sdbuf_request_addr = 0;

            snprintf(cmd, 1024, "sffd3086 %02x\n", saved_side);
            slow_write(fd, cmd, strlen(cmd));
            if (!halt)
              start_cpu();
          }
        }
        if (addr == 0xffd3659) {
          fprintf(stderr, "Hypervisor virtualisation flags = $%02x\n", b[0]);
          if (virtual_f011 && hypervisor_paused)
            restart_hyppo();
          hypervisor_paused = 0;
          printf("hyperv not paused\n");
        }
      }
    }
    if ((!strcmp(line, " :0000800 A0 A0 A0 A0 A0 A0 A0 A0 A0 A0 A0 A0 A0 A0 A0 A0"))
        || (!strcmp(line, ":00000800:A0A0A0A0A0A0A0A0A0A0A0A0A0A0A0A0"))) {

      if (modeline_cmd[0]) {
#ifdef WINDOWS
        fprintf(stderr, "[T+%I64dsec] Setting video modeline\n", (long long)time(0) - start_time);
#else
        fprintf(stderr, "[T+%lldsec] Setting video modeline\n", (long long)time(0) - start_time);
#endif
        fprintf(stderr, "Commands:\n%s\n", modeline_cmd);
        slow_write(fd, modeline_cmd, strlen(modeline_cmd));

        // Disable on-screen keyboard to be sure
        do_usleep(50000);
        slow_write(fd, "sffd3615 7f\n", 12);

        // Force mode change to take effect, after first giving time for VICIV to recalc parameters
        do_usleep(50000);
        slow_write(fd, "sffd3011 1b\n", 12);

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
	    do_usleep(50000);
	    read_and_print(fd);
	  }
#endif

        // Then ask for current mode information via VIC-IV registers, but first give a little time
        // for the mode change to take effect
        do_usleep(100000);
        slow_write(fd, "Mffd3040\n", 9);
      }
      else if (mode_report) {
        slow_write(fd, "Mffd3040\n", 9);
      }
    }
    else {
      if (!saw_c65_mode)
        fprintf(stderr, "MEGA65 is in C65 mode.\n");
      saw_c65_mode = 1;
      if ((!do_go64) && filename && not_already_loaded) {
        printf("XXX Trying to load from C65 mode\n");
        char* cmd;
        cmd = "bf664\r";
        slow_write(fd, cmd, strlen(cmd));
        stuff_keybuffer("DLo\"!\r");
#ifdef WINDOWS
        if (first_load)
          fprintf(stderr, "[T+%I64dsec] Injecting LOAD\"!\n", (long long)time(0) - start_time);
#else
        if (first_load)
          fprintf(stderr, "[T+%lldsec] Injecting LOAD\"!\n", (long long)time(0) - start_time);
#endif
        first_load = 0;

        while (state != 1) {
          process_waiting(fd);
        }
      }
      else if ((!mode_report) && (!virtual_f011) && (!type_text)) {
        if (do_run) {
          // C65 mode stuff keyboard buffer
          printf("XXX - Do C65 keyboard buffer stuffing\n");
          //	} else if (screen_shot) {
          //	  if (!screen_address) {
          //	    printf("Waiting to capture screen...\n");
          //	    // We need to get some info about the screen
          //	    slow_write_safe(fd,"Mffd3058\r",9);
          //	  }
        }
        else {
          fprintf(stderr, "Exiting now that we are in C65 mode.\n");
          do_exit(0);
        }
      }
    }
  }
  if ( // C64 BASIC banner
      (!strcmp(line, " :000042C 2A 2A 2A 2A 20 03 0F 0D 0D 0F 04 0F 12 05 20 36"))
      || (!strcmp(line, ":0000042C:2A2A2A2A20030F0D0D0F040F12052036"))
      // MEGA BASIC banner
      || (!strcmp(line, " :000042C 2A 2A 2A 2A 20 0D 05 07 01 36 35 20 0D 05 07 01"))
      || (!strcmp(line, ":0000042C:2A2A2A2A200D0507013635200D050701"))) {
    // C64 mode BASIC -- set LOAD trap, and then issue LOAD command
    char* cmd;
    if (filename && not_already_loaded) {
      cmd = "bf4a5\r";
      saw_c64_mode = 1;
      slow_write(fd, cmd, strlen(cmd));
      stuff_keybuffer("Lo\"!\",8,1\r");
      // We are waiting for a breakpoint that will almost certainly come soon.
      cpu_stopped = 1;
#ifdef WINDOWS
      if (first_load)
        fprintf(stderr, "[T+%I64dsec] LOAD\"!\n", (long long)time(0) - start_time);
#else
      if (first_load)
        fprintf(stderr, "[T+%lldsec] LOAD\"!\n", (long long)time(0) - start_time);
#endif
      first_load = 0;
    }
    else {
      if (!saw_c64_mode)
        fprintf(stderr, "MEGA65 is in C64 mode.\n");
      saw_c64_mode = 1;
      if ((!virtual_f011)) // &&(!screen_shot))
        do_exit(0);
      //      if (screen_shot) slow_write_safe(fd,"Mffd3058\r",9);
    }
  }
  if (state == 2) {
    state = 99;
    printf("Filename is %s\n", filename);
    if (!virtual_f011)
      do_exit(0);
  }
}
return 0;
}
