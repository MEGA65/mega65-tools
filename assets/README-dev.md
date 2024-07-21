
# MEGA65 Tools - automatic build

This package contains a pre-release build of the mega65-tools. Use with caution
and report errors via [GitHub](https://github.com/MEGA65/mega65-tools/issues). If
unsure, use the stable builds you can find on Filehost:

- [MEGA65 Tools Release Package (Linux)](https://files.mega65.org?id=e97f3bf4-9d55-4ae2-a39a-d31dd15e8d34)
- [MEGA65 Tools Release Package (MacOS)](https://files.mega65.org?id=57f855b9-a758-49df-ba7c-d120c4d1241d)
- [MEGA65 Tools Release Package (Windows)](https://files.mega65.org?id=06c55815-7826-4ad6-be0e-b8dc5e721b6d)

All tools have some sort of help, so try starting them with `-h` or without
parameters to get some idea how to use them, or read the manual to get more
information.

## Communication Tools

- `m65`: swiss army knife of tools for automated actions to perform on MEGA65
  hardware. This requires an UART or JTAG USB adapter.
- `etherload`: communicates with the MEGA65 via Ethernet over IPv6.
- `mega65_ftp`: an ftp tool to more easily put and get files to/from your sd-card.
  This requires either a serial connection (via UART or JTAG USB adapter) or can also
  work over Ethernet (like `ethertool`, IPv6).

## Build Tools

- `coretool`: a combined tool to work with core files.
  - `bit2core` *(deprecated!)*: converts bitstreams into core files (.cor)
  - `bit2mcs` *(deprecated!)*: converts bitstreams (or core files) into the Vivado MCS format
- `romdiff`: romdiff can create RDF ROM patches and apply them
