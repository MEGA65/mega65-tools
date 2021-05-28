.SUFFIXES: .bin .prg
.PRECIOUS:	%.ngd %.ncd %.twx vivado/%.xpr bin/%.bit bin/%.mcs bin/%.M65 bin/%.BIN

#COPT=	-Wall -g -std=gnu99 -fsanitize=address -fno-omit-frame-pointer -fsanitize-address-use-after-scope
#CC=	clang
COPT=	-Wall -g -std=gnu99 -I/opt/local/include -L/opt/local/lib -I/usr/local/include/libusb-1.0 -L/usr/local/lib -mno-sse3 -march=x86-64
# -I/usr/local/Cellar/libusb/1.0.23/include/libusb-1.0/ -L/usr/local/Cellar/libusb/1.0.23/lib/libusb-1.0/
CC=	gcc
CXX=g++
WINCC=	x86_64-w64-mingw32-gcc
WINCOPT=$(COPT) -DWINDOWS -D__USE_MINGW_ANSI_STDIO=1

OPHIS=	Ophis/bin/ophis
OPHISOPT=	-4 --no-warn
OPHIS_MON= Ophis/bin/ophis -c

JAVA = java
KICKASS_JAR = KickAss/KickAss.jar

VIVADO=	./vivado_wrapper

ACME=	/usr/local/bin/acme

ifdef USE_LOCAL_CC65
	# use locally installed binary (requires 'cc65,ld65,etc' to be in the $PATH)
	CC65_PREFIX=
else
	# use the binary built from the submodule
	CC65_PREFIX=$(PWD)/cc65/bin/
endif

CC65=  $(CC65_PREFIX)cc65
CA65=  $(CC65_PREFIX)ca65 --cpu 4510
LD65=  $(CC65_PREFIX)ld65 -t none
CL65=  $(CC65_PREFIX)cl65 --config src/tests/vicii.cfg
CC65_DEPEND=

CBMCONVERT=	cbmconvert/cbmconvert

ASSETS=		assets
SRCDIR=		src
BINDIR=		bin
B65DIR=		bin65
EXAMPLEDIR=	$(SRCDIR)/examples
UTILDIR=	$(SRCDIR)/utilities
TESTDIR=	$(SRCDIR)/tests
LIBEXECDIR=	libexec
GTESTDIR=gtest
GTESTBINDIR=$(GTESTDIR)/bin
# For now, disable g++ compile warnings on tests (there's so many :))
GTESTOPTS = -w -DTESTING

SDCARD_DIR=	sdcard-files

# if you want your PRG to appear on "MEGA65.D81", then put your PRG in "./d81-files"
# ie: COMMANDO.PRG
#
# if you want your .a65 source code compiled and then embedded within "MEGA65.D81", then put
# your source.a65 in the 'utilities' directory, and ensure it is listed below.
#
# NOTE: that all files listed below will be embedded within the "MEGA65.D81".
#
UTILITIES=	$(B65DIR)/ethertest.prg \
		$(B65DIR)/etherload.prg \
		$(B65DIR)/rompatch.prg \
		$(B65DIR)/wirekrill.prg \
		$(B65DIR)/b65support.bin \
		$(B65DIR)/cartload.prg

TESTS=		$(TESTDIR)/ascii.prg \
		$(TESTDIR)/vicii.prg \
		$(TESTDIR)/test_290.prg \
		$(TESTDIR)/test_332.prg \
		$(TESTDIR)/test_334.prg \
		$(TESTDIR)/test_335.prg \
		$(TESTDIR)/test_342.prg \
		$(TESTDIR)/test_378.prg \
		$(TESTDIR)/test_390.prg

TOOLDIR=	$(SRCDIR)/tools
TOOLS=	$(BINDIR)/etherload \
	$(BINDIR)/m65 \
	$(BINDIR)/mega65_ftp \
	$(BINDIR)/monitor_save \
	$(BINDIR)/romdiff \
	$(BINDIR)/pngprepare \
	$(BINDIR)/giftotiles \
	$(BINDIR)/m65ftp_test \
	$(BINDIR)/bit2core \
	$(BINDIR)/bit2mcs \
	$(BINDIR)/m65.exe \
	$(BINDIR)/mega65_ftp.exe \
	$(BINDIR)/bit2core.exe \
	$(BINDIR)/bit2mcs.exe \
	$(BINDIR)/romdiff.exe

SDCARD_FILES=	$(SDCARD_DIR)/M65UTILS.D81 \
		$(SDCARD_DIR)/M65TESTS.D81

all:	$(SDCARD_FILES) $(TOOLS) $(UTILITIES) $(TESTS)

$(SDCARD_DIR)/FREEZER.M65:
	git submodule init
	git submodule update
	( cd src/mega65-freezemenu && make FREEZER.M65 )
	cp src/mega65-freezemenu/FREEZER.M65 $(SDCARD_DIR)

$(CBMCONVERT):
	git submodule init
	git submodule update
	( cd cbmconvert && make -f Makefile.unix )

ifndef USE_LOCAL_CC65
$(CC65):
	git submodule init
	git submodule update
	( cd cc65 && make -j 8 )
endif

$(OPHIS):
	git submodule init
	git submodule update

# c-programs
tools:	$(TOOLS)

# assembly files (a65 -> prg)
utilities:	$(UTILITIES)

$(TOOLDIR)/merge-issue:	$(TOOLDIR)/merge-issue.c
	$(CC) $(COPT) -o $(TOOLDIR)/merge-issue $(TOOLDIR)/merge-issue.c

$(TOOLDIR)/vhdl-path-finder:	$(TOOLDIR)/vhdl-path-finder.c
	$(CC) $(COPT) -o $(TOOLDIR)/vhdl-path-finder $(TOOLDIR)/vhdl-path-finder.c

$(TOOLDIR)/osk_image:	$(TOOLDIR)/osk_image.c
	$(CC) $(COPT) -I/usr/local/include -L/usr/local/lib -o $(TOOLDIR)/osk_image $(TOOLDIR)/osk_image.c -lpng

$(TOOLDIR)/frame2png:	$(TOOLDIR)/frame2png.c
	$(CC) $(COPT) -I/usr/local/include -L/usr/local/lib -o $(TOOLDIR)/frame2png $(TOOLDIR)/frame2png.c -lpng


# verbose, for 1581 format, overwrite
$(SDCARD_DIR)/M65UTILS.D81:	$(UTILITIES) $(CBMCONVERT)
	$(info =============================================================)
	$(info ~~~~~~~~~~~~~~~~> Making: $(SDCARD_DIR)/MEGA65.D81)
	mkdir -p $(SDCARD_DIR)
	$(CBMCONVERT) -v2 -D8o $(SDCARD_DIR)/M65UTILS.D81 $(UTILITIES)

# verbose, for 1581 format, overwrite
$(SDCARD_DIR)/M65TESTS.D81:	$(TESTS) $(CBMCONVERT)
	$(info =============================================================)
	$(info ~~~~~~~~~~~~~~~~> Making: $(SDCARD_DIR)/MEGA65.D81)
	mkdir -p $(SDCARD_DIR)
	$(CBMCONVERT) -v2 -D8o $(SDCARD_DIR)/M65TESTS.D81 $(TESTS)

# ============================ done moved, print-warn, clean-target
# ophis converts the *.a65 file (assembly text) to *.prg (assembly bytes)
%.prg:	%.a65 $(OPHIS)
	$(info =============================================================)
	$(info ~~~~~~~~~~~~~~~~> Making: $@)
	$(OPHIS) $(OPHISOPT) $< -l $*.list -m $*.map -o $*.prg


bin65/ethertest.prg:	$(UTILDIR)/ethertest.a65 $(OPHIS)
	$(OPHIS) $(OPHISOPT) $< -l $*.list -m $*.map -o $*.prg

%.prg:	utilities/%.a65 $(OPHIS)
	$(info =============================================================)
	$(info ~~~~~~~~~~~~~~~~> Making: $@)
	$(OPHIS) $(OPHISOPT) utilities/$< -l $*.list -m $*.map -o $*.prg

%.bin:	%.a65 $(OPHIS)
	$(info =============================================================)
	$(info ~~~~~~~~~~~~~~~~> Making: $@)
	$(OPHIS) $(OPHISOPT) $< -l $*.list -m $*.map -o $*.prg

%.o:	%.s $(CC65)
	$(CA65) $< -l $*.list

$(UTILDIR)/mega65_config.o:      $(UTILDIR)/mega65_config.s $(UTILDIR)/mega65_config.inc $(CC65)
	$(CA65) $< -l $*.list

bin65/b65support.bin:	$(UTILDIR)/b65support.a65 $(OPHIS)
	$(OPHIS) $(OPHISOPT) $< -l $*.list -m $*.map -o $*.bin

$(TESTDIR)/vicii.prg:       $(TESTDIR)/vicii.c $(TESTDIR)/vicii_asm.s $(CC65)
	$(CL65) -O -o $*.prg --mapfile $*.map $< $(TESTDIR)/vicii_asm.s

$(UTILDIR)/gmod2-tools.prg:       $(UTILDIR)/gmod2-tools.c $(CC65)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(TESTDIR)/test_290.prg:       $(TESTDIR)/test_290.c $(CC65)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(TESTDIR)/test_332.prg:       $(TESTDIR)/test_332.c $(CC65)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(TESTDIR)/test_334.prg:       $(TESTDIR)/test_334.c $(CC65)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(TESTDIR)/test_335.prg:       $(TESTDIR)/test_335.c $(CC65)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(TESTDIR)/test_342.prg:       $(TESTDIR)/test_342.c $(CC65)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(TESTDIR)/test_378.prg:       $(TESTDIR)/test_378.c $(CC65)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(TESTDIR)/test_390.prg:       $(TESTDIR)/test_390.c $(CC65)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(TESTDIR)/test_332.prg:       $(TESTDIR)/test_332.c $(CC65)
$(TESTDIR)/buffereduart.prg:       $(TESTDIR)/buffereduart.c $(CC65)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -Iinclude/ -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(TESTDIR)/pulseoxy.prg:       $(TESTDIR)/pulseoxy.c $(CC65)
	$(CL65) -O -o $*.prg --mapfile $*.map $< 

$(TESTDIR)/qspitest.prg:       $(TESTDIR)/qspitest.c $(CC65)
	$(CL65) -O -o $*.prg --mapfile $*.map $< 

$(EXAMPLES)/unicorns.prg:       $(EXAMPLES)/unicorns.c $(CC65)
	$(CL65) -O -o $*.prg --mapfile $*.map $<

$(EXAMPLEDIR)/bmpview.prg:       $(EXAMPLEDIR)/bmpview.c $(CC65)
	$(CL65)  -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(TESTDIR)/eth_mdio.prg:       $(TESTDIR)/eth_mdio.c $(CC65)
	$(CL65) -O -o $*.prg --mapfile $*.map $< 

$(TESTDIR)/instructiontiming.prg:       $(TESTDIR)/instructiontiming.c $(TESTDIR)/instructiontiming_asm.s $(CC65)
	$(CL65) -O -o $*.prg --mapfile $*.map $< $(TESTDIR)/instructiontiming_asm.s

$(UTILDIR)/mega65_config.prg:       $(UTILDIR)/mega65_config.o $(CC65)
	$(LD65) $< --mapfile $*.map -o $*.prg

$(UTILDIR)/megaflash.prg:       $(UTILDIR)/megaflash.c $(CC65)
	git submodule init
	git submodule update
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(UTILDIR)/megaphonenorflash.prg:       $(UTILDIR)/megaphonenorflash.c $(CC65)
	git submodule init
	git submodule update
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(UTILDIR)/remotesd.prg:       $(UTILDIR)/remotesd.c $(CC65)
	git submodule init
	git submodule update
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --listing $*.list --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(TESTDIR)/floppytest.prg:       $(TESTDIR)/floppytest.c $(CC65)
	git submodule init
	git submodule update
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(UTILDIR)/c65toc64wrapper.prg:	$(UTILDIR)/c65toc64wrapper.asm $(ACME)
	$(ACME) --setpc 0x2001 --cpu m65 --format cbm --outfile $(UTILDIR)/c65toc64wrapper.prg $(UTILDIR)/c65toc64wrapper.asm

$(EXAMPLEDIR)/verticalrasters.prg:	$(EXAMPLEDIR)/verticalrasters.asm $(ACME)
	$(ACME) --setpc 0x0801 --cpu m65 --format cbm --outfile $(EXAMPLEDIR)/verticalrasters.prg $(EXAMPLEDIR)/verticalrasters.asm

$(UTILDIR)/fastload.prg:       $(UTILDIR)/fastload.c $(CC65)
	git submodule init
	git submodule update
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(EXAMPLEDIR)/modplay.prg:       $(EXAMPLEDIR)/modplay.c $(CC65)
	git submodule init
	git submodule update
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(TESTDIR)/r3_production_test.prg:       $(TESTDIR)/r3_production_test.c $(CC65)
	git submodule init
	git submodule update
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(B65DIR)/wirekrill.prg:       $(EXAMPLEDIR)/wirekrill.c $(CC65)
	git submodule init
	git submodule update
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

bin/wirekrill:	$(EXAMPLEDIR)/wirekrill.c
	$(CC) $(COPT) -DNATIVE_TEST -I/usr/local/include -L/usr/local/lib -o $(BINDIR)/wirekrill $(EXAMPLEDIR)/wirekrill.c -lpcap

$(B65DIR)/cartload.prg:       $(UTILDIR)/cartload.c $(CC65)
	git submodule init
	git submodule update
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(B65DIR)/rompatch.prg:       $(UTILDIR)/rompatch.c $(CC65)
	git submodule init
	git submodule update
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(EXAMPLEDIR)/raycaster.prg:       $(EXAMPLEDIR)/raycaster.c $(CC65)
	git submodule init
	git submodule update
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(TESTDIR)/ultrasoundtest.prg:       $(TESTDIR)/ultrasoundtest.c $(CC65)
	git submodule init
	git submodule update
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(TESTDIR)/opcodes65.prg:       $(TESTDIR)/opcodes65.c $(CC65)
	git submodule init
	git submodule update
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(UTILDIR)/avtest.prg:       $(UTILDIR)/avtest.c $(CC65)
	git submodule init
	git submodule update
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s






$(TESTDIR)/hyperramtest.prg:       $(TESTDIR)/hyperramtest.c $(CC65) $(wildcard $(SRCDIR)/mega65-libc/cc65/src/*.c) $(wildcard $(SRCDIR)/mega65-libc/cc65/src/*.s) $(wildcard $(SRCDIR)/mega65-libc/cc65/include/*.h)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $< $(wildcard $(SRCDIR)/mega65-libc/cc65/src/*.c) $(wildcard $(SRCDIR)/mega65-libc/cc65/src/*.s)

$(UTILDIR)/i2clist.prg:       $(UTILDIR)/i2clist.c $(CC65)
	$(CL65) $< --mapfile $*.map -o $*.prg

$(UTILDIR)/i2cstatus.prg:       $(UTILDIR)/i2cstatus.c $(CC65)  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s $(SRCDIR)/mega65-libc/cc65/include/*.h
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(UTILDIR)/floppystatus.prg:       $(UTILDIR)/floppystatus.c $(CC65)
	$(CL65) $< --mapfile $*.map -o $*.prg

$(UTILDIR)/tiles.prg:       $(UTILDIR)/tiles.o $(CC65)
	$(LD65) $< --mapfile $*.map -o $*.prg

$(UTILDIR)/diskmenuprg.o:      $(UTILDIR)/diskmenuprg.a65 $(UTILDIR)/diskmenu.a65 $(UTILDIR)/diskmenu_sort.a65 $(CC65)
	$(CA65) $< -l $*.list

$(UTILDIR)/diskmenu.prg:       $(UTILDIR)/diskmenuprg.o $(CC65)
	$(LD65) $< --mapfile $*.map -o $*.prg

$(SRCDIR)/mega65-fdisk/m65fdisk.prg:
	( cd $(SRCDIR)/mega65-fdisk ; make m65fdisk.prg )

$(BINDIR)/border.prg: 	$(SRCDIR)/border.a65 $(OPHIS)
	$(OPHIS) $(OPHISOPT) $< -l $(BINDIR)/border.list -m $*.map -o $(BINDIR)/border.prg

# ============================ done moved, print-warn, clean-target
#??? diskmenu_c000.bin yet b0rken
$(BINDIR)/HICKUP.M65: $(SRCDIR)/hyppo/main.asm $(SRCDIR)/version.asm
	$(JAVA) -jar $(KICKASS_JAR) $< -afo

$(SRCDIR)/monitor/monitor_dis.a65: $(SRCDIR)/monitor/gen_dis
	$(SRCDIR)/monitor/gen_dis >$(SRCDIR)/monitor/monitor_dis.a65

$(BINDIR)/monitor.m65:	$(SRCDIR)/monitor/monitor.a65 $(SRCDIR)/monitor/monitor_dis.a65 $(SRCDIR)/monitor/version.a65
	$(OPHIS_MON) $< -l monitor.list -m monitor.map

# ============================ done moved, print-warn, clean-target
$(UTILDIR)/diskmenuc000.o:     $(UTILDIR)/diskmenuc000.a65 $(UTILDIR)/diskmenu.a65 $(UTILDIR)/diskmenu_sort.a65 $(CC65)
	$(CA65) $< -l $*.list

$(BINDIR)/diskmenu_c000.bin:   $(UTILDIR)/diskmenuc000.o $(CC65)
	$(LD65) $< --mapfile $*.map -o $*.bin

$(B65DIR)/etherload.prg:	$(UTILDIR)/etherload.a65 $(OPHIS)
	$(OPHIS) $(OPHISOPT) $< -l $*.list -m $*.map -o $*.prg

$(SRCDIR)/open-roms/build/mega65.rom:	$(SRCDIR)/open-roms/assets/8x8font.png
	( cd $(SRCDIR)/open-roms ; make build/mega65.rom )

$(SRCDIR)/open-roms/assets/8x8font.png:
	git submodule init
	git submodule update
	( cd $(SRCDIR)/open-roms ; git submodule init ; git submodule update )

$(BINDIR)/asciifont.bin:	$(BINDIR)/pngprepare $(ASSETS)/ascii00-7f.png
	$(BINDIR)/pngprepare charrom $(ASSETS)/ascii00-7f.png $(BINDIR)/asciifont.bin

$(BINDIR)/charrom.bin:	$(BINDIR)/pngprepare $(ASSETS)/8x8font.png
	$(BINDIR)/pngprepare charrom $(ASSETS)/8x8font.png $(BINDIR)/charrom.bin

# ============================ done moved, Makefile-dep, print-warn, clean-target
# c-code that makes an executable that processes images, and can make a vhdl file
$(BINDIR)/pngprepare:	$(TOOLDIR)/pngprepare/pngprepare.c Makefile
	$(CC) $(COPT) -I/usr/local/include -L/usr/local/lib -o $(BINDIR)/pngprepare $(TOOLDIR)/pngprepare/pngprepare.c -lpng

$(BINDIR)/romdiff:	$(TOOLDIR)/romdiff.c Makefile
	$(CC) $(COPT) -O3 -I/usr/local/include -L/usr/local/lib -o $(BINDIR)/romdiff $(TOOLDIR)/romdiff.c

$(BINDIR)/romdiff.exe:	$(TOOLDIR)/romdiff.c Makefile
	$(WINCC) $(WINCOPT) -g -Wall -Iinclude -I/usr/include/libusb-1.0 -I/opt/local/include/libusb-1.0 -I/usr/local//Cellar/libusb/1.0.18/include/libusb-1.0/ -I$(TOOLDIR)/fpgajtag/ -o $(BINDIR)/romdiff.exe $(TOOLDIR)/romdiff.c -Wl,-Bstatic -Wl,-Bdynamic

$(BINDIR)/romdiff.osx:	$(TOOLDIR)/romdiff.c Makefile
	$(CC) $(COPT) -D__APPLE__ -g -Wall -Iinclude -o $(BINDIR)/romdiff.osx $(TOOLDIR)/romdiff.c

$(BINDIR)/giftotiles:	$(TOOLDIR)/pngprepare/giftotiles.c Makefile
	$(CC) $(COPT) -I/usr/local/include -L/usr/local/lib -o $(BINDIR)/giftotiles $(TOOLDIR)/pngprepare/giftotiles.c -lgif

# Utility to make hi-colour displays from PNGs, with upto 256 colours per char row
$(BINDIR)/pnghcprepare:	$(TOOLDIR)/pngprepare/pnghcprepare.c Makefile
	$(CC) $(COPT) -I/usr/local/include -L/usr/local/lib -o $(BINDIR)/pnghcprepare $(TOOLDIR)/pngprepare/pnghcprepare.c -lpng

# Utility to make hi-colour displays from PNGs, with upto 256 colours per char row
$(BINDIR)/md2h65:	$(TOOLDIR)/pngprepare/md2h65.c Makefile
	$(CC) $(COPT) -I/usr/local/include -L/usr/local/lib -o $(BINDIR)/md2h65 $(TOOLDIR)/pngprepare/md2h65.c -lpng


$(BINDIR)/utilpacker:	$(BINDIR)/utilpacker.c Makefile
	$(CC) $(COPT) -o $(BINDIR)/utilpacker $(TOOLDIR)/utilpacker/utilpacker.c

/usr/bin/convert: 
	echo "Could not find the program 'convert'. Try the following:"
	echo "sudo apt-get install imagemagick"

$(TOOLDIR)/version.c: .FORCE
	echo 'const char *version_string="'`./gitversion.sh`'";' > $(TOOLDIR)/version.c

.FORCE:

$(SDCARD_DIR)/BANNER.M65:	$(BINDIR)/pngprepare $(ASSETS)/mega65_320x64.png /usr/bin/convert
	/usr/bin/convert -colors 128 -depth 8 +dither $(ASSETS)/mega65_320x64.png $(BINDIR)/mega65_320x64_128colour.png
	$(BINDIR)/pngprepare logo $(BINDIR)/mega65_320x64_128colour.png $(SDCARD_DIR)/BANNER.M65

$(BINDIR)/m65ftp_test:	$(TESTDIR)/m65ftp_test.c
	$(CC) $(COPT) -g -Wall -o $(BINDIR)/m65ftp_test $(TESTDIR)/m65ftp_test.c

$(BINDIR)/m65:	$(TOOLDIR)/m65.c $(TOOLDIR)/m65common.c $(TOOLDIR)/version.c $(TOOLDIR)/screen_shot.c $(TOOLDIR)/fpgajtag/*.c $(TOOLDIR)/fpgajtag/*.h Makefile
	$(CC) $(COPT) -g -Wall -Iinclude -I/usr/include/libusb-1.0 -I/opt/local/include/libusb-1.0 -I/usr/local//Cellar/libusb/1.0.18/include/libusb-1.0/ -o $(BINDIR)/m65 $(TOOLDIR)/m65.c $(TOOLDIR)/m65common.c $(TOOLDIR)/version.c $(TOOLDIR)/screen_shot.c $(TOOLDIR)/fpgajtag/fpgajtag.c $(TOOLDIR)/fpgajtag/util.c $(TOOLDIR)/fpgajtag/process.c -lusb-1.0 -lz -lpthread -lpng

$(BINDIR)/m65.osx:	$(TOOLDIR)/m65.c $(TOOLDIR)/m65common.c $(TOOLDIR)/version.c $(TOOLDIR)/screen_shot.c $(TOOLDIR)/fpgajtag/*.c $(TOOLDIR)/fpgajtag/*.h Makefile
	$(CC) $(COPT) -D__APPLE__ -g -Wall -Iinclude -I/usr/include/libusb-1.0 -I/opt/local/include/libusb-1.0 -I/usr/local//Cellar/libusb/1.0.18/include/libusb-1.0/ -o $(BINDIR)/m65.osx $(TOOLDIR)/m65.c $(TOOLDIR)/m65common.c $(TOOLDIR)/version.c $(TOOLDIR)/screen_shot.c $(TOOLDIR)/fpgajtag/fpgajtag.c $(TOOLDIR)/fpgajtag/util.c $(TOOLDIR)/fpgajtag/process.c -lusb-1.0 -lz -lpthread -lpng


$(BINDIR)/m65.exe:	$(TOOLDIR)/m65.c $(TOOLDIR)/m65common.c $(TOOLDIR)/version.c $(TOOLDIR)/screen_shot.c $(TOOLDIR)/fpgajtag/*.c $(TOOLDIR)/fpgajtag/*.h Makefile
	$(WINCC) $(WINCOPT) -g -Wall -Iinclude `pkg-config --cflags libusb-1.0` -I/usr/include/libusb-1.0 -I/opt/local/include/libusb-1.0 -I/usr/local//Cellar/libusb/1.0.18/include/libusb-1.0/ -I$(TOOLDIR)/fpgajtag/ -o $(BINDIR)/m65.exe $(TOOLDIR)/m65.c $(TOOLDIR)/m65common.c $(TOOLDIR)/version.c $(TOOLDIR)/screen_shot.c $(TOOLDIR)/fpgajtag/fpgajtag.c $(TOOLDIR)/fpgajtag/util.c $(TOOLDIR)/fpgajtag/process.c -Wl,-Bstatic -lusb-1.0 -lwsock32 -lws2_32 -lpng -lz -Wl,-Bdynamic
# $(TOOLDIR)/fpgajtag/listComPorts.c $(TOOLDIR)/fpgajtag/disphelper.c

$(LIBEXECDIR)/ftphelper.bin:	$(TOOLDIR)/ftphelper.a65
	$(OPHIS) $(OPHISOPT) $(TOOLDIR)/ftphelper.a65

$(TOOLDIR)/ftphelper.c:	$(UTILDIR)/remotesd.prg $(TOOLDIR)/bin2c
	$(TOOLDIR)/bin2c $(UTILDIR)/remotesd.prg helperroutine $(TOOLDIR)/ftphelper.c

define LINUX_AND_MINGW_GTEST_TARGETS
$(1): $(2)
	$$(CXX) $$(COPT) $$(GTESTOPTS) -Iinclude -o $$@ $$(filter %.c %.cpp,$$^) -lreadline -lncurses -lgtest_main -lgtest -lpthread

$(1).exe: $(2)
	$$(CXX) $$(WINCOPT) $$(GTESTOPTS) -Iinclude -o $$@ $$(filter %.c %.cpp,$$^) -lreadline -lncurses -lgtest_main -lgtest -lpthread -Wl,-Bstatic -lwsock32 -lws2_32 -lz -Wl,-Bdynamic
endef

# Gives two targets of:
# - gtest/bin/mega65_ftp.test
# - gtest/bin/mega65_ftp.test.exe
$(eval $(call LINUX_AND_MINGW_GTEST_TARGETS, $(GTESTBINDIR)/mega65_ftp.test, $(GTESTDIR)/mega65_ftp_test.cpp $(TOOLDIR)/mega65_ftp.c $(TOOLDIR)/m65common.c $(TOOLDIR)/ftphelper.c Makefile))

# Gives two targets of:
# - gtest/bin/bit2core.test
# - gtest/bin/bit2core.test.exe
$(eval $(call LINUX_AND_MINGW_GTEST_TARGETS, $(GTESTBINDIR)/bit2core.test, $(GTESTDIR)/bit2core_test.cpp $(TOOLDIR)/bit2core.c $(TOOLDIR)/version.c))

test: $(GTESTBINDIR)/mega65_ftp.test $(GTESTBINDIR)/bit2core.test
	$(GTESTBINDIR)/mega65_ftp.test
	$(GTESTBINDIR)/bit2core.test

test.exe: $(GTESTBINDIR)/mega65_ftp.test.exe $(GTESTBINDIR)/bit2core.test.exe
	$(GTESTBINDIR)/mega65_ftp.test.exe
	$(GTESTBINDIR)/bit2core.test.exe

$(BINDIR)/mega65_ftp:	$(TOOLDIR)/mega65_ftp.c $(TOOLDIR)/m65common.c Makefile $(TOOLDIR)/ftphelper.c
	$(CC) $(COPT) -Iinclude -o $(BINDIR)/mega65_ftp $(TOOLDIR)/mega65_ftp.c $(TOOLDIR)/m65common.c $(TOOLDIR)/ftphelper.c -lreadline -lncurses

$(BINDIR)/mega65_ftp.static:	$(TOOLDIR)/mega65_ftp.c $(TOOLDIR)/m65common.c Makefile $(TOOLDIR)/ftphelper.c ncurses/lib/libncurses.a readline/libreadline.a readline/libhistory.a
	$(CC) $(COPT) -Iinclude -mno-sse3 -o $(BINDIR)/mega65_ftp.static $(TOOLDIR)/mega65_ftp.c $(TOOLDIR)/m65common.c $(TOOLDIR)/ftphelper.c ncurses/lib/libncurses.a readline/libreadline.a readline/libhistory.a -ltermcap

$(BINDIR)/mega65_ftp.exe:	$(TOOLDIR)/mega65_ftp.c Makefile $(TOOLDIR)/ftphelper.c $(TOOLDIR)/m65common.c
	$(WINCC) $(WINCOPT) -g -Wall -Iinclude -I/usr/include/libusb-1.0 -I/opt/local/include/libusb-1.0 -I/usr/local//Cellar/libusb/1.0.18/include/libusb-1.0/ -I$(TOOLDIR)/fpgajtag/ -o $(BINDIR)/mega65_ftp.exe $(TOOLDIR)/mega65_ftp.c $(TOOLDIR)/m65common.c $(TOOLDIR)/ftphelper.c -lusb-1.0 -Wl,-Bstatic -lwsock32 -lws2_32 -lz -Wl,-Bdynamic

$(BINDIR)/mega65_ftp.osx:	$(TOOLDIR)/mega65_ftp.c $(TOOLDIR)/ftphelper.c Makefile $(TOOLDIR)/m65common.c
	$(CC) $(COPT) -D__APPLE__ -g -Wall -Iinclude -I/usr/include/libusb-1.0 -I/opt/local/include/libusb-1.0 -I/usr/local//Cellar/libusb/1.0.18/include/libusb-1.0/ -o $(BINDIR)/mega65_ftp.osx $(TOOLDIR)/mega65_ftp.c $(TOOLDIR)/m65common.c $(TOOLDIR)/ftphelper.c -lusb-1.0 -lz -lpthread -lreadline

$(BINDIR)/bitinfo:	$(TOOLDIR)/bitinfo.c Makefile 
	$(CC) $(COPT) -g -Wall -o $(BINDIR)/bitinfo $(TOOLDIR)/bitinfo.c


# Create targets for binary (linux) and binary.exe (mingw) easily, minimising repetition
# arg1 = target name (without .exe)
# arg2 = pre-requisites
define LINUX_AND_MINGW_TARGETS
$(1): $(2)
	$$(CC) -g -Wall -Iinclude -o $$@ $$(filter %.c,$$^)

$(1).exe: $(2)
	$$(WINCC) $$(WINCOPT) -g -Wall -Iinclude -o $$@ $$(filter %.c,$$^)
endef

# Creates 2 targets:
# - bin/bit2core (for linux)
# - bin/bit2core.exe (for mingw)
$(eval $(call LINUX_AND_MINGW_TARGETS, $(BINDIR)/bit2core, $(TOOLDIR)/bit2core.c $(TOOLDIR)/version.c Makefile))

$(BINDIR)/bit2mcs:	$(TOOLDIR)/bit2mcs.c Makefile 
	$(CC) $(COPT) -g -Wall -o $(BINDIR)/bit2mcs $(TOOLDIR)/bit2mcs.c

$(BINDIR)/bit2mcs.exe:	$(TOOLDIR)/bit2mcs.c Makefile 
	$(WINCC) $(WINCOPT) -g -Wall -o $(BINDIR)/bit2mcs $(TOOLDIR)/bit2mcs.c

$(BINDIR)/monitor_save:	$(TOOLDIR)/monitor_save.c Makefile
	$(CC) $(COPT) -o $(BINDIR)/monitor_save $(TOOLDIR)/monitor_save.c

#-----------------------------------------------------------------------------

$(BINDIR)/ethermon:	$(TOOLDIR)/ethermon.c
	$(CC) $(COPT) -o $(BINDIR)/ethermon $(TOOLDIR)/ethermon.c -I/usr/local/include -lpcap

$(BINDIR)/etherload:	$(TOOLDIR)/etherload/etherload.c
	$(CC) $(COPT) -o $(BINDIR)/etherload $(TOOLDIR)/etherload/etherload.c -I/usr/local/include

$(BINDIR)/videoproxy:	$(TOOLDIR)/videoproxy.c
	$(CC) $(COPT) -o $(BINDIR)/videoproxy $(TOOLDIR)/videoproxy.c -I/usr/local/include -lpcap

$(BINDIR)/vncserver:	$(TOOLDIR)/vncserver.c
	$(CC) $(COPT) -O3 -o $(BINDIR)/vncserver $(TOOLDIR)/vncserver.c -I/usr/local/include -lvncserver -lpthread

format:
	find . -type d \( -path ./cc65 -o -path ./cbmconvert \) -prune -false -o -iname '*.h' -o -iname '*.c' -o -iname '*.cpp' | xargs clang-format --style=file -i

clean:
	rm -f $(SDCARD_FILES) $(TOOLS) $(UTILITIES) $(TESTS) $(UTILDIR)/*.prg

cleangen:	clean

