# m65dbg
The “m65dbg” tool is a command-line based enhanced remote serial debugger/monitor for the mega65 project. In brief, some of its facilities include:

* Capable of being a symbolic debugger (for code compiled with Ophis, ACME or CC65)
* Disassemble any point of memory (showing original file-listing, if available)
* Print out memory values in byte, word, dword or string form
* Watch such memory values continuously as you debug and step through code
* Save and load chunks of memory between the MEGA65 and your local PC
* Provide a backtrace that you can comfortably move up and down through
* Search through memory for a sequence of bytes or string
* Take petscii screenshots of current screen context (borrowed from m65 tool)
* Remote-typing mode (borrowed from m65 tool)
* FTP access to SD-card (borrowed from mega65\_ftp tool)
* You can also connect m65dbg to the serial-monitor provided by the xemu mega65 emulator

# Latest documentation

Latest detailed documentation can be found here:

* https://docs.google.com/document/d/1cEhHvvc1E47UgSoXvKPpwnf43l20sLPY-y-ZuQ4b\_ng/edit?usp=sharing

# The raw monitor

The m65dbg app adds enhanced commands on-top of the existing commands provided by the raw monitor.

Documentation for this raw monitor exists for it in the following locations:

* C65GS System Notes
  * https://docs.google.com/document/d/1fmEUg6hDdWRb2tFZ3n4LG7S1mNP04\_SUAW5DrE8zRpk/edit#
  * See the “**4.3 Remote Serial Monitor (handy for debugging)**” section
* https://github.com/Ben-401/c65gs/blob/dockit/doc/monitor.md
  * Some extra usage info exists in this page of Ben's fork of the github repo
* Typing “?” within the monitor (via your terminal app) to see a list of slightly outdated commands

The source for the m65dbg app is available here:

* https://github.com/MEGA65/m65dbg

# Video walkthroughs of m65dbg

These videos are highly-recommended watching, in order to get quickly acquainted with the facilities available in the m65dbg tool:

* m65dbg walkthrough
  * https://www.youtube.com/watch?v=2VT8yB3odhg
* m65dbg updates
  * https://www.youtube.com/watch?v=Rumti6AzsKY

For a youtube video walkthrough, please visit:

* https://www.youtube.com/watch?v=2VT8yB3odhg

# Walkthrough

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

