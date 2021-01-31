# mega65-tools
Tools and Utilities for the MEGA65 Retro Computers

To build on e.g. Debian Linux, install the following packages:

```
apt-get install git build-essential libusb-dev install libpng-dev libusb-1.0-0-dev libreadline-dev libgif-dev
```

If you want to cross-build the tools for Windows, you'll also need:

```
apt-get install binutils-mingw-w64 mingw-w64-common gcc-mingw-w64
```

After cloning the repository, enter its directory and call

```
make
```

