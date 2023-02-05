
# MEGA65 Tools - automatic build

This package contains a pre-release build of the mega65-tools. Use with caution
and report errors via [GitHub](https://github.com/MEGA65/mega65-tools/issues). If
unsure, use the stable builds you can find on
[Filehost](https://files.mega65.org?id=e3ec875c-ca31-44fc-a268-ab6f9990e004).

All tools have some sort of help, so try starting them with `-h` or without
parameters to get some idea how to use them, or read the manual to get more 
information.

## Communication Tools

- `m65`: swiss army knife of tools for automated actions to perform on mega65
  hardware, requires UART or JTAG USB adapter.
- `mega65_ftp`: an ftp tool to more easily put and get files to/from your sd-card,
  requires UART or JTAG USB adapter.
- **BETA** `etherload`: communicates with the MEGA65 via Ethernet, requires 
  development core.

## Build Tools

- `bit2core`: convertes bitstreams into core files (.cor)
- `bit2mcs`: converts bitstreams (or core files) into the Vivado MCS format
- `romdiff`: romdiff can create RDF ROM patches and apply them
