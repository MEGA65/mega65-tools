# m65dbg
An enhanced remote serial debugger/monitor for the mega65 project

* Provide disassembly of output from remote serial output
* Can display your source-code as you debug
  * This requires that you build your assembly source via a modified version of Ophis that enhances the \*.list file output
    * **git clone https://github.com/gardners/Ophis.git**
* allows uploading of a local file directly into mega65 memory via the **load** command
* allows dumping out of data to a local file via the **save** command
* Use the **back** command to get a backstrace of the stack, then use **up** and **down** to navigate through the stack
* Many commands can take symbolic references if you tell the Ophis compiler to save out a .map files

## Future Wishlist
* How about live assembly, direct into the mega65's memory?

# The raw monitor

The m65dbg app adds enhanced commands on-top of the existing commands provided by the raw monitor.

Documentation for this raw monitor exists for it in the following locations:

* C65GS System Notes
  * https://docs.google.com/document/d/1fmEUg6hDdWRb2tFZ3n4LG7S1mNP04_SUAW5DrE8zRpk/edit#
  * See the “**4.3 Remote Serial Monitor (handy for debugging)**” section
* https://github.com/Ben-401/c65gs/blob/dockit/doc/monitor.md
  * Some extra usage info exists in this page of Ben's fork of the github repo
* Typing “?” within the monitor (via your terminal app) to see a list of slightly outdated commands

The source for the m65dbg app is available here:

* https://github.com/MEGA65/m65dbg

# Building

* You will need to install a few pre-requisite libraries (via apt-get/yum/zypper, or cygwin's setup.exe)
  * **libreadline-dev**
  * **libpng-dev**
* **git clone https://github.com/MEGA65/m65dbg**
* **cd m65dbg**
* **make**
  * This will produce the “**m65dbg**” executable (or, “**m65dbg.exe**”, in Cygwin's case)

# Walkthrough

For a youtube video walkthrough, please visit:

* https://www.youtube.com/watch?v=2VT8yB3odhg

Here's some written points below:

* Should build fine in Windows+Cygwin and Linux.
* Mac OSX presently doesn't support the 2,000,000 bps serial speed. So as an alternative, you can make m65dbg instead talk to the xemu mega65 emulator via a unix socket.
  * More info at: 
* Specify your serial port with the "**-l**" (or "**--device**) parameter, E.g., "**m65dbg -l /dev/ttyUSB1**"

Try the following steps:

* **./m65dbg -l /dev/ttyUSB1**
* **r** (to print out current registers)
* **b<addr>** = a raw command to set a hardware breakpoint
* **t1** (to turn trace mode on, a bit like ctrl-c breaking inside gdb)
* **t0** (to turn trace mode off, a bit like doing 'c'/continue in gdb)
* **n** (my 'next'/step-over command, which uses the hardware's step-into command multiple times, can be very slow)
* **s** (my 'step-into' command, which just calls the hardware's step-into)
* **[ENTER]** key will repeat the last command
* **finish** (my 'step-out-of' command, which calls 'n'/next multiple times, can be very slow, depending on how busy the current function is)
* **pb/pw/pd/ps** <addr> = prints byte/word/dword/string values at the given address
* typing “**help**” will give you the list of m65dbg commands, while typing “**?**” will give you the list of raw commands.

