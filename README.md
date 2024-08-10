# MEGA65 Tools

Tools and Utilities for the MEGA65 Retro Computers, such as:

- `m65`: swiss army knife of tools for automated actions to perform on mega65 hardware
- `mega65_ftp`: an ftp tool to more easily put and get files to/from your sd-card
- **BETA** `etherload`: communicates with the MEGA65 via Ethernet, requires 
  development core.
- `coretool`: a combined tool to work with core container files
  - `bit2core` *(deprecated!)*: convertes bitstreams into core files (.cor)
  - `bit2mcs` *(deprecated!)*: converts bitstreams (or core files) into the Vivado MCS format
- `romdiff`: romdiff can create RDF ROM patches and apply them

# Download

Stable builds are available for download on
[Filehost](https://files.mega65.org?id=e3ec875c-ca31-44fc-a268-ab6f9990e004)

# Building under Linux

To build on e.g. Debian Linux, install the following packages:

```
sudo apt-get install build-essential pkg-config git gh wget python3 cc65 sshpass unzip imagemagick p7zip-full libgtest-dev libgmock-dev libreadline-dev libtinfo5 libcairo2-dev
```

After cloning the repository, enter its directory and call

```
make all
```
to build native **Linux** binaries.

If you want to cross-build the tools for **Windows**, you'll also need:

```
sudo apt-get update
sudo apt install -y gcc-mingw-w64 mingw-w64-common libz-mingw-w64-dev cmake python3-pip libusb-1.0-0-dev libgif-dev libcairo2-dev
pip install conan
```

Execute the Windows cross-build by calling (if you do this often, you can set `WIN_CROSS_BUILD` to `1` in your environment)

```
make WIN_CROSS_BUILD=1 allwin
```

For developers that want to commit code to the repo, it's suggested you also install clang-format version 11.

```
sudo apt-get install clang-format-11
sudo update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-11 10
sudo update-alternatives --config clang-format
```

If your official Ubuntu apt repositories only contain older versions, you can download a statically-linked version of v11 from either:

-   https://github.com/eozer/clang-tools-static-binaries/releases/download/master-369f669d/clang-format-11_linux-amd64
-   https://github.com/gurcei/clang-tools-static-binaries/releases/download/untagged-3be9747e6ae6df232d87/clang-format-11_linux-amd64

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

## Option 1: Install WSL:
-   https://ubuntu.com/tutorials/install-ubuntu-on-wsl2-on-windows-11-with-gui-support#1-overview
-   Runt the tutorial and then continue with [Building under Linux](#building-under-linux)

## Option 2: Installing msys2:

-   https://www.msys2.org/#installation
-   Run it from: `installpath/msys64/mingw64.exe`

## Option 3: Install Git for Windows:

-   https://git-scm.com/download/win
-   Run "**Git Bash**", type in `mintty &` and select the "**Mingw-w64 64 bit**" option

## Installing pre-requisites (for options 2 and 3)

After doing so, you can then install the build pre-requisites in mingw64 by typing:

`pacman -S make mingw-w64-x86_64-toolchain mingw-w64-x86_64-libusb`

If you chose the msys2 option and don't have Git, then do:

`pacman -S git`

You should then be able to build with `make`.

If you want to build specific tools, you can run targets like:

-   `make bin/mega65_ftp.exe`
-   `make bin/m65.exe`

For developers that want to commit code to the repo, it's suggested you also install clang, in order to run clang-format (it presently provides clang-format v11).

```
pacman -S clang
```

If you prefer to use a statically-linked binary instead, you can download it from either:

-   https://github.com/eozer/clang-tools-static-binaries/releases/download/master-369f669d/clang-format-11_windows-amd64.exe
-   https://github.com/gurcei/clang-tools-static-binaries/releases/download/untagged-3cdbf8ef732ddf71ebdf/clang-format-11_windows-amd64.exe

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

-   **FROM**: `#include <libusb.h>`
-   **TO**: `#include <libusb-1.0/libusb.h>`

I'll try sort out that error at some later stage...

# Building under macOS

To build for macOS, install the following packages using [Homebrew](https://brew.sh/):

```
brew install conan
```

To build all of the tools currently available for macOS:

```
make allmac
```

You can also make these individually:

```
make bin/m65.osx
make bin/mega65_ftp.osx
make bin/romdiff.osx
make bin/m65dbg.osx
make bin/etherload.osx
```

Other tools within the suite may or may not compile with their equivalent Linux targets.

For developers that want to commit code to the repo, it's suggested that you install clang-format-11. On a Mac, you can use [Homebrew](https://brew.sh/) to install it:

```
brew install clang-format
```

With `clang-format` installed, you can apply the enforced style to the source files with the `format` make target:

```
make format
```

# Unit Tests

Some initial effort is underway to start providing unit tests for our tooling, starting with mega65_ftp and bit2core.

It presently makes use of gtest release 1.10.0.

To install gtest on linux run the following:

```
sudo apt install cmake
git clone https://github.com/google/googletest.git -b release-1.10.0
cd googletest
mkdir build
cd build
cmake ..
make
sudo make install
```

Unit tests are housed in the 'gtest/' folder.

The unit test executables are housed in the 'gtest/bin' folder.

To generate the tests and run them:

#### Linux:

```
make test
```

#### Windows

```
make test.exe
```
