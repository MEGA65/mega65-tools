
# MEGA65 Tools - Release 1.00

This package contains the 1.00 release build of the mega65-tools.

Please report bugs or file feature requests via
[GitHub](https://github.com/MEGA65/mega65-tools/issues).

All tools have some sort of help, so try starting them with `-h` or without
parameters to get some idea how to use them, or read the manual to get more
information.

## Communication Tools

- `m65`: swiss army knife of tools for automated actions to perform on mega65
  hardware, requires UART or JTAG USB adapter.
- `etherload`: communicates with the MEGA65 via Ethernet over IPv6, requires
  development core.
- `mega65_ftp`: an ftp tool to more easily put and get files to/from your sd-card,
  requires either a serial connection (via UART or JTAG USB adapter) or can also
  work over Ethernet (like `ethertool`, IPv6).

## Build Tools

- `coretool`: a combined tool to work with core container files
  - `bit2core` *(deprecated!)*: convertes bitstreams into core files (.cor)
  - `bit2mcs` *(deprecated!)*: converts bitstreams (or core files) into the Vivado MCS format
- `romdiff`: romdiff can create RDF ROM patches and apply them
