# m65dbg
An enhanced remote serial debugger/monitor for the mega65 project

I'll try to document my various goals/ambitions/ideas here.

* Provide disassembly of output from remote serial output
  * I can use m<addr> or d<addr> command and parse the resulting memory dump output to generate and display the equivalent assembly commands
  * I might need to use Ophis source to (perhaps as a reference) to help out with accomplishing this disassembly
* allow uploading of compiled data directly into memory via the serial monitor interface
* provide some ability to assess the stack easily, perhaps with up/down commands as in gdb (if possible)
* make use of .map files output from Ophis, to try substitute address values with symbol-names
* How about live assembly, direct into the mega65's memory?
* Offer redirection of command output to a file
  * This could provide an easy way to dump current memory contents to a file
