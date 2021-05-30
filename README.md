# mega65-tools

Tools and Utilities for the MEGA65 Retro Computers, such as:

- **m65**: swiss army knife of tools for automated actions to perform on mega65 hardware
- **mega65_ftp**: an ftp tool to more easily put and get files to/from your sd-card
- **bit2core**: a tool for converting bitstreams into core files (.cor)

# Building under Linux

To build on e.g. Debian Linux, install the following packages:

```
sudo apt-get install git build-essential libusb-dev libpng-dev libusb-1.0-0-dev libreadline-dev libgif-dev libncurses5-dev
```

If you want to cross-build the tools for Windows, you'll also need:

```
sudo apt-get install binutils-mingw-w64 mingw-w64-common gcc-mingw-w64 libz-mingw-w64-dev
sudo apt-key adv --keyserver keyserver.ubuntu.com --recv-keys F192CFC5C989ADAE
sudo add-apt-repository "deb http://gurce.net/ubuntu/ bionic main"
sudo apt-get update
sudo apt install -y libpng-mingw-w64 libusb-1.0-0-mingw-w64
```

After cloning the repository, enter its directory and call

```
make
```

For developers that want to commit code to the repo, it's suggested you also install clang-format version 11.

```
sudo apt-get install clang-format-11
sudo update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-11 10
sudo update-alternatives --config clang-format
```

If your official Ubuntu apt repositories only contain older versions, you can download a statically-linked version of v11 from either:

- https://github.com/eozer/clang-tools-static-binaries/releases/download/master-369f669d/clang-format-11_linux-amd64
- https://github.com/gurcei/clang-tools-static-binaries/releases/download/untagged-3be9747e6ae6df232d87/clang-format-11_linux-amd64

Then do:
```
sudo update-alternatives --install /usr/bin/clang-format clang-format /path/to/downloaded/clang-format-11_linux-amd64 20
```

You can then apply the enforced style by typing:

```
make format
```

# Building under Windows

You will need to prepare a mingw64 environment for windows.

Two possible means of getting mingw64 are:

## Option 1: Installing msys2:
- https://www.msys2.org/#installation
- Run it from: `installpath/msys64/mingw64.exe`

## Option 2: Install Git for Windows:
- https://git-scm.com/download/win
- Run "**Git Bash**", type in `mintty &` and select the "**Mingw-w64 64 bit**" option


## Installing pre-requisites

After doing so, you can then install the build pre-requisites in mingw64 by typing:

`pacman -S make mingw-w64-x86_64-toolchain mingw-w64-x86_64-libusb`

If you chose the msys2 option and don't have Git, then do:

`pacman -S git`

You should then be able to build with `make`.

If you want to build specific tools, you can run targets like:
- `make bin/mega65_ftp.exe`
- `make bin/m65.exe`

For developers that want to commit code to the repo, it's suggested you also install clang, in order to run clang-format (it presently provides clang-format v11).

```
pacman -S clang
```

If you prefer to use a statically-linked binary instead, you can download it from either:

- https://github.com/eozer/clang-tools-static-binaries/releases/download/master-369f669d/clang-format-11_windows-amd64.exe
- https://github.com/gurcei/clang-tools-static-binaries/releases/download/untagged-3cdbf8ef732ddf71ebdf/clang-format-11_windows-amd64.exe

You can then apply the enforced style by typing:

```
make format
```

## Known compiler error for mingw64

If you get this error compiling:

```
src/tools/fpgajtag/util.c:43:10: fatal error: libusb.h: No such file or directory
```

Then tweak this "fpgajtag/util.c" as follows:

* __FROM__: `#include <libusb.h>`
* __TO__: `#include <libusb-1.0/libusb.h>`

I'll try sort out that error at some later stage...


# Building under Mac OSX

Still a bit in flux at this stage, but there are a few individual make targets available already:

- `make bin/m65.osx`
- `make bin/mega65_ftp.osx`

Other tools within the suite may or may not compile with their equivalent linux targets, haven't confirmed at this stage...

For developers that want to commit code to thie repo, it's suggested that you install clang-format-11.

Perhaps homebrew offers an install of this (haven't checked).

Alternately, you can download statically-linked binaries from either:

- https://github.com/eozer/clang-tools-static-binaries/releases/download/master-369f669d/clang-format-11_macosx-amd64
- https://github.com/gurcei/clang-tools-static-binaries/releases/download/untagged-22ae84d665118b63c4b1/clang-format-11_macosx-amd64

You can then apply the enforced style by typing:

```
make format
```

# Unit tests

Some initial effort is underway to start providing unit tests for our tooling, starting with mega65_ftp.

It presently makes use of gtest release 1.10.0.

Unit tests are housed in the 'gtest/' folder.

The unit test executables are housed in the 'gtest/bin' folder.

E.g. to generate the executable:

```
make gtest/bin/mega65_ftp.test
```

To run the unit tests:

```
cd gtest/bin
./mega65_ftp.test
```
