
# MEGA65 Tools - Release 1.00

This package contains the 1.00 release build of the mega65-tools.

Please report bugs or file feature requests via
[GitHub](https://github.com/MEGA65/mega65-tools/issues).

All tools have some sort of help, so try starting them with `-h` or without
parameters to get some idea how to use them, or read the manual to get more
information.

## Communication Tools

- `m65`: swiss army knife of tools for automated actions to perform on MEGA65
  hardware. This requires an UART or JTAG USB adapter.
- `etherload`: communicates with the MEGA65 via Ethernet over IPv6.
- `mega65_ftp`: a file transfer tool to easily put and get files to/from your sd-card.
  This requires either a serial connection (via UART or JTAG USB adapter) or can also
  work over Ethernet (like `ethertool`, IPv6).

## Build Tools

- `coretool`: a combined tool to work with core files.
  - `bit2core` *(deprecated!)*: converts bitstreams into core files (.cor)
  - `bit2mcs` *(deprecated!)*: converts bitstreams (or core files) into the Vivado MCS format
- `romdiff`: romdiff can create RDF ROM patches and apply them

## Development Builds

Automatic development builds of mega65-tools are available on filehost. They
might offer fancier features, but they can also be a bit unstable.

- [MEGA65 Tools Development Build (Linux)](https://files.mega65.org?id=2b7bd912-1181-447c-a489-223f16b764c1)
- [MEGA65 Tools Development Build (MacOS)](https://files.mega65.org?id=7d96641c-b306-49cf-80ff-ea1e5d00c9d1)
- [MEGA65 Tools Development Build (Windows)](https://files.mega65.org?id=658322fd-e586-4b4f-a991-89470b269b4a)
