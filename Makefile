.SUFFIXES: .bin .prg
.PRECIOUS:	%.ngd %.ncd %.twx vivado/%.xpr bin/%.bit bin/%.mcs bin/%.M65 bin/%.BIN

OS := $(shell uname)
ifeq ($(OS), Darwin)
include conanbuildinfo_macos_intel.mak
include conanbuildinfo_macos_arm.mak
endif

#COPT=	-Wall -g -std=gnu99 -fsanitize=address -fno-omit-frame-pointer -fsanitize-address-use-after-scope
#CC=	clang
COPT=	-Wall -g -std=gnu99
# -I/usr/local/Cellar/libusb/1.0.23/include/libusb-1.0/ -L/usr/local/Cellar/libusb/1.0.23/lib/libusb-1.0/
CC=	gcc
CXX=g++
#MACCOPT=$(COPT) -framework CoreFoundation -framework IOKit
MACINTELCOPT:=$(COPT) -target x86_64-apple-macos10.14 \
                      $(addprefix -I, $(MAC_INTEL_CONAN_INCLUDE_DIRS)) \
                      $(addprefix -D, $(MAC_INTEL_CONAN_DEFINES)) \
                      $(addprefix -L, $(MAC_INTEL_CONAN_LIB_DIRS)) \
                      $(addprefix -l, $(MAC_INTEL_CONAN_LIBS)) \
		      $(addprefix -framework , $(MAC_INTEL_CONAN_FRAMEWORKS))
MACARMCOPT:=  $(COPT) -target arm64-apple-macos11 \
                      $(addprefix -I, $(MAC_ARM_CONAN_INCLUDE_DIRS)) \
                      $(addprefix -D, $(MAC_ARM_CONAN_DEFINES)) \
                      $(addprefix -L, $(MAC_ARM_CONAN_LIB_DIRS)) \
                      $(addprefix -l, $(MAC_ARM_CONAN_LIBS)) \
		      $(addprefix -framework , $(MAC_ARM_CONAN_FRAMEWORKS))
COPT+=`pkg-config --cflags-only-I --libs-only-L libusb-1.0 libpng` -mno-sse3 -march=x86-64
WINCC=	x86_64-w64-mingw32-gcc
WINCOPT=$(COPT) -DWINDOWS -D__USE_MINGW_ANSI_STDIO=1

OPHIS=	Ophis/bin/ophis
OPHISOPT=	-4 --no-warn
OPHIS_MON= Ophis/bin/ophis -c

ACME=	/usr/local/bin/acme

ifdef USE_LOCAL_CC65
	# use locally installed binary (requires 'cc65,ld65,etc' to be in the $PATH)
	CC65_PREFIX=
else
	# use the binary built from the submodule
	CC65_PREFIX=$(PWD)/cc65/bin/
endif

BUILD_STATIC=-Wl,-Bstatic

ifeq ($(DO_STATIC), 0)
	# Does user want compile dynamically? (faster, better during development)
	BUILD_STATIC=
endif

CC65=  $(CC65_PREFIX)cc65
CA65=  $(CC65_PREFIX)ca65 --cpu 4510
LD65=  $(CC65_PREFIX)ld65 -t none
CL65=  $(CC65_PREFIX)cl65 --config src/tests/tests_cl65.cfg
CL65ONLY=  $(CC65_PREFIX)cl65
CC65_DEPEND=

LIBUSBINC= `pkg-config --cflags libusb-1.0` -I/usr/include/libusb-1.0 -I/opt/local/include/libusb-1.0 -I/usr/local/Cellar/libusb/1.0.18/include/libusb-1.0/
MACLIBUSBLINK= `pkg-config --variable libdir libusb-1.0`/libusb-1.0.a -framework Security
MACLIBPNGLINK= `pkg-config --variable libdir libpng`/libpng.a

CBMCONVERT=	cbmconvert/cbmconvert

ASSETS=		assets
SRCDIR=		src
BINDIR=		bin
B65DIR=		bin65
EXAMPLEDIR=	$(SRCDIR)/examples
UTILDIR=	$(SRCDIR)/utilities
BASICUTILDIR=	$(SRCDIR)/basic-utilities
TESTDIR=	$(SRCDIR)/tests
LIBEXECDIR=	libexec
SDCARD_DIR=	sdcard-files
GTESTDIR=	gtest
GTESTBINDIR=	$(GTESTDIR)/bin
# For now, disable g++ compile warnings on tests (there's so many :))
GTESTOPTS=	-w -DTESTING

#
# if you want your BASIC PRG to appear on "M65UTILS.D81",
# then put your PRG in "./src/basic-utilities" and list it in BASICUTILS
#
# if you want your compiles PRG appear on "M65UTILS.D81",
# then put it into src/utilities, and list it in UTILITIES
#
UTILITIES=	$(B65DIR)/ethertest.prg \
		$(B65DIR)/etherload.prg \
		$(B65DIR)/rompatch.prg \
		$(B65DIR)/wirekrill.prg \
		$(B65DIR)/b65support.bin \
		$(B65DIR)/cartload.prg

# these are BASIC dumped from the MEGA65
BASICUTILS=	$(BASICUTILDIR)/mega65info.prg \
		$(BASICUTILDIR)/rtctest.prg

#
# all tests will be gathered in "M65TESTS.D81"
#
# make sure that your test conforms to the UNIT TEST Framework
#
# tests used for automatic core regression testing need to be listed in
#   src/tests/regression-tests.lst
#
TESTS=		$(TESTDIR)/ascii.prg \
		$(TESTDIR)/vicii.prg \
		$(TESTDIR)/test_290.prg \
		$(TESTDIR)/test_332.prg \
		$(TESTDIR)/test_334.prg \
		$(TESTDIR)/test_335.prg \
		$(TESTDIR)/test_342.prg \
		$(TESTDIR)/test_378.prg \
		$(TESTDIR)/test_390.prg \
		$(TESTDIR)/test_454.prg \
		$(TESTDIR)/test_458.prg \
		$(TESTDIR)/test_459.prg \
		$(TESTDIR)/test_468.prg \
		$(TESTDIR)/test_495.prg \
		$(TESTDIR)/test_497.prg \
		$(TESTDIR)/test_535.prg \
		$(TESTDIR)/test_579.prg \
		$(TESTDIR)/test_585.prg \
		$(TESTDIR)/test_340.prg \
		$(TESTDIR)/test_604.prg \
		$(TESTDIR)/test_mandelbrot.prg \
		$(TESTDIR)/eth_rxd_test.prg \

TOOLDIR=	$(SRCDIR)/tools

TOOLSUNX=	$(BINDIR)/etherload \
		$(BINDIR)/m65 \
		$(BINDIR)/readdisk \
		$(BINDIR)/mega65_ftp \
		$(BINDIR)/romdiff \
		$(BINDIR)/pngprepare \
		$(BINDIR)/giftotiles \
		$(BINDIR)/m65ftp_test \
		$(BINDIR)/mfm-decode \
		$(BINDIR)/bit2core \
		$(BINDIR)/bit2mcs

TOOLSWIN=	$(BINDIR)/m65.exe \
		$(BINDIR)/mega65_ftp.exe \
		$(BINDIR)/bit2core.exe \
		$(BINDIR)/bit2mcs.exe \
		$(BINDIR)/romdiff.exe

TOOLSMAC=	$(BINDIR)/m65.osx \
		$(BINDIR)/mega65_ftp.osx \
		$(BINDIR)/bit2core.osx \
		$(BINDIR)/bit2mcs.osx \
		$(BINDIR)/romdiff.osx \
		$(BINDIR)/m65dbg.osx

GTESTFILES=	$(GTESTBINDIR)/mega65_ftp.test \
		$(GTESTBINDIR)/bit2core.test

GTESTFILESEXE=	$(GTESTBINDIR)/mega65_ftp.test.exe \
		$(GTESTBINDIR)/bit2core.test.exe

# all dependencies
MEGA65LIBC= $(wildcard $(SRCDIR)/mega65-libc/cc65/src/*.c) $(wildcard $(SRCDIR)/mega65-libc/cc65/src/*.s) $(wildcard $(SRCDIR)/mega65-libc/cc65/include/*.h)

# TOOLS omits TOOLSMAC. Linux users can make all. To make Mac binaries, use
# a Mac to make allmac. See README.md.
TOOLS=$(TOOLSUNX) $(TOOLSWIN)

SDCARD_FILES=	$(SDCARD_DIR)/M65UTILS.D81 \
		$(SDCARD_DIR)/M65TESTS.D81

SUBMODULEUPDATE= \
	@if [ -z "$(DO_SMU)" ] || [ "$(DO_SMU)" -eq "1" ] ; then \
	echo "Updating Submodules... (set env-var DO_SMU=0 to turn this behaviour off)" ; \
	git submodule update --init ; \
	fi

##
## Global Rules
##
.PHONY: all allunix allmac allwin arcwin arcmac arcunix tests tools utilities format clean cleanall cleantest

all:	$(SDCARD_FILES) $(TOOLS) $(UTILITIES) $(TESTS)
allmac:	$(SDCARD_FILES) $(TOOLSMAC) $(UTILITIES) $(TESTS)
allwin:	$(SDCARD_FILES) $(TOOLSWIN) $(UTILITIES) $(TESTS)
allunix:	$(SDCARD_FILES) $(TOOLSUNX) $(UTILITIES) $(TESTS)

arcunix: allunix
	arcdir=m65tools-`$(SRCDIR)/gitversion.sh arcname`-linux; \
	if [[ -e $${arcdir} ]]; then \
		rm -rf $${arcdir} $${arcdir}.7z ; \
	fi ; \
	mkdir -p $${arcdir}/bin $${arcdir}/sdcard-files $${arcdir}/mega65 ; \
	ln $(TOOLSUNX) $${arcdir}/bin ; \
	ln $(SDCARD_FILES) $${arcdir}/sdcard-files ; \
	ln $(UTILITIES) $${arcdir}/mega65 ; \
	7z a $${arcdir}.7z $${arcdir} ; \
	rm -rf $${arcdir}

arcwin: allwin
	arcdir=m65tools-`$(SRCDIR)/gitversion.sh arcname`-windows; \
	if [[ -e $${arcdir} ]]; then \
		rm -rf $${arcdir} $${arcdir}.7z ; \
	fi ; \
	mkdir -p $${arcdir}/bin $${arcdir}/sdcard-files $${arcdir}/mega65 ; \
	ln $(TOOLSWIN) $${arcdir}/bin ; \
	ln $(SDCARD_FILES) $${arcdir}/sdcard-files ; \
	ln $(UTILITIES) $${arcdir}/mega65 ; \
	7z a $${arcdir}.7z $${arcdir} ; \
	rm -rf $${arcdir}

arcmac: allmac
	arcdir=m65tools-`$(SRCDIR)/gitversion.sh arcname`-macos; \
	if [[ -e $${arcdir} ]]; then \
		rm -rf $${arcdir} $${arcdir}.7z ; \
	fi ; \
	mkdir -p $${arcdir}/bin $${arcdir}/sdcard-files $${arcdir}/mega65 ; \
	ln $(TOOLSMAC) $${arcdir}/bin ; \
	ln $(SDCARD_FILES) $${arcdir}/sdcard-files ; \
	ln $(UTILITIES) $${arcdir}/mega65 ; \
	7z a $${arcdir}.7z $${arcdir} ; \
	rm -rf $${arcdir}

# all the tests
tests: $(TESTS)

# c-programs
tools:	$(TOOLS)

# assembly files (a65 -> prg)
utilities:	$(UTILITIES)

format:
	@submodules=""; for sm in `git submodule | awk '{ print "./" $$2 }'`; do \
		submodules="$$submodules -o -path $$sm"; \
	done; \
	find . -type d \( $${submodules:3} \) -prune -false -o \( -iname '*.h' -o -iname '*.c' -o -iname '*.cpp' \) -print | xargs clang-format --style=file -i --verbose

clean:	cleantest
	rm -f src/tools/version.c $(SDCARD_FILES) $(TOOLS) $(UTILITIES) $(TESTS) $(UTILDIR)/*.prg $(TESTDIR)/*.o $(EXAMPLEDIR)/*.o m65tools-*.7z

cleanall:	clean
	for path in `git submodule | awk '{ print "./" $$2 }'`; do \
		if [ -e $$path/Makefile ]; then make -C $$path clean; fi; \
	done

cleantest:
	rm -f dummy.txt *.d81

# testing
test: $(GTESTFILES)
	@for test in $+; do \
		name=$${test%%.test}; \
		name=$${name##*/}; \
		echo ""; \
		echo "TESTING: $$name..."; \
		echo "======================"; \
		$$test; \
	done

test.exe: $(GTESTFILESEXE)
	@for test in $+; do \
		name=$${test%%.test.exe}; \
		name=$${name##*/}; \
		path=$${test%/*}; \
		echo ""; \
		echo "TESTING: $$name..."; \
		echo "======================"; \
		cd $$path; ./$${test##*/}; \
	done

##
## Prerequisites
##
conanbuildinfo_macos_intel.mak: conanfile.txt conan/*
	conan install conanfile.txt --build=missing -pr:h=default -pr:h=conan/profile_macos_10.14_intel
	sed 's/CONAN_/MAC_INTEL_CONAN_/g' conanbuildinfo.mak > conanbuildinfo_macos_intel.mak
	rm conanbuildinfo.*

conanbuildinfo_macos_arm.mak: conanfile.txt conan/*
	conan install conanfile.txt --build=missing -pr:h=default -pr:h=conan/profile_macos_11_arm
	sed 's/CONAN_/MAC_ARM_CONAN_/g' conanbuildinfo.mak > conanbuildinfo_macos_arm.mak
	rm conanbuildinfo.*

ifndef USE_LOCAL_CC65
$(CC65):
	$(SUBMODULEUPDATE)
	( cd cc65 && make -j 8 )
endif

$(OPHIS):
	$(SUBMODULEUPDATE)

$(CBMCONVERT):
	$(SUBMODULEUPDATE)
	( cd cbmconvert && make -f Makefile.unix )

/usr/bin/convert:
	echo "Could not find the program 'convert'. Try the following:"
	echo "sudo apt-get install imagemagick"

$(TOOLDIR)/version.c: $(SRCDIR)/gitversion.sh
	@if [ -z "$(DO_MKVER)" ] || [ "$(DO_MKVER)" -eq "1" ] ; then \
		echo "Retrieving Git version string... (set env-var DO_MKVER=0 to turn this behaviour off)" ; \
		echo 'const char *version_string="'`$(SRCDIR)/gitversion.sh`'";' > $(TOOLDIR)/version.c ; \
	fi

##
## Disk images
##
$(SDCARD_DIR)/M65UTILS.D81:	$(CBMCONVERT) $(UTILITIES) $(BASICUTILS)
	$(info =============================================================)
	$(info ~~~~~~~~~~~~~~~~> Making: $(SDCARD_DIR)/M65UTILS.D81)
	$(CBMCONVERT) -v2 -D8o $(SDCARD_DIR)/M65UTILS.D81 $(UTILITIES) $(BASICUTILS)

$(SDCARD_DIR)/M65TESTS.D81:	$(CBMCONVERT) $(TESTS)
	$(info =============================================================)
	$(info ~~~~~~~~~~~~~~~~> Making: $(SDCARD_DIR)/M65TESTS.D81)
	$(CBMCONVERT) -v2 -D8o $(SDCARD_DIR)/M65TESTS.D81 $(TESTS)

##
## Global wildcard rules
##
%.prg:	%.a65 $(OPHIS)
	$(info =============================================================)
	$(info ~~~~~~~~~~~~~~~~> Making: $@)
	$(OPHIS) $(OPHISOPT) $< -l $*.list -m $*.map -o $*.prg


%.bin:	%.a65 $(OPHIS)
	$(info =============================================================)
	$(info ~~~~~~~~~~~~~~~~> Making: $@)
	$(OPHIS) $(OPHISOPT) $< -l $*.list -m $*.map -o $*.prg

%.o:	%.s $(CC65)
	$(CA65) $< -l $*.list

$(BINDIR)/%.osx:	$(BINDIR)/%_intel.osx $(BINDIR)/%_arm.osx
	lipo -create -output $@ $(BINDIR)/$*_intel.osx $(BINDIR)/$*_arm.osx

##
## TESTS
##
#
# TODO: src/tests/379-attic-ram.prg src/tests/test_361.prg
#
# common test rule
$(TESTDIR)/%.prg:	$(TESTDIR)/%.c include/*.h $(CC65) $(MEGA65LIBC)
	$(CL65) -I include/ -I $(SRCDIR)/mega65-libc/cc65/include -O -o $(TESTDIR)/$*.prg --mapfile $(TESTDIR)/$*.map $< $(SRCDIR)/mega65-libc/cc65/src/memory.c $(SRCDIR)/mega65-libc/cc65/src/tests.c

# asm test rule
$(TESTDIR)/%.prg:       $(TESTDIR)/%.s $(TESTDIR)/unittestlog.s
	$(CL65ONLY) -t none -o $@ $<

# tests that need additional pices of code
$(TESTDIR)/vicii.prg:       $(TESTDIR)/vicii.c $(TESTDIR)/vicii_asm.s $(CC65) $(MEGA65LIBC)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $< $(TESTDIR)/vicii_asm.s $(SRCDIR)/mega65-libc/cc65/src/memory.c $(SRCDIR)/mega65-libc/cc65/src/tests.c

$(TESTDIR)/test_290.prg:       $(TESTDIR)/test_290.c $(CC65) $(MEGA65LIBC)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $< $(SRCDIR)/mega65-libc/cc65/src/memory.c $(SRCDIR)/mega65-libc/cc65/src/random.c $(SRCDIR)/mega65-libc/cc65/src/tests.c

$(TESTDIR)/test_585.prg:       $(TESTDIR)/test_585.c $(TESTDIR)/test_585_asm.s $(CC65) $(MEGA65LIBC)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $< $(TESTDIR)/test_585_asm.s $(SRCDIR)/mega65-libc/cc65/src/memory.c $(SRCDIR)/mega65-libc/cc65/src/tests.c

$(TESTDIR)/test_604.prg:       $(TESTDIR)/test_604.c $(TESTDIR)/test_604_asm.s $(CC65) $(MEGA65LIBC)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $< $(TESTDIR)/test_604_asm.s $(SRCDIR)/mega65-libc/cc65/src/memory.c $(SRCDIR)/mega65-libc/cc65/src/tests.c

$(TESTDIR)/buffereduart.prg:       $(TESTDIR)/buffereduart.c $(CC65) $(MEGA65LIBC)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -Iinclude/ -O -o $*.prg --mapfile $*.map $< $(SRCDIR)/mega65-libc/cc65/src/memory.c $(SRCDIR)/mega65-libc/cc65/src/hal.c $(SRCDIR)/mega65-libc/cc65/src/targets.c

$(TESTDIR)/pulseoxy.prg:       $(TESTDIR)/pulseoxy.c $(CC65) $(MEGA65LIBC)
	$(CL65) -O -o $*.prg --mapfile $*.map $<

$(TESTDIR)/eth_mdio.prg:       $(TESTDIR)/eth_mdio.c $(CC65) $(MEGA65LIBC)
	$(CL65) -O -o $*.prg --mapfile $*.map $<

$(TESTDIR)/instructiontiming.prg:       $(TESTDIR)/instructiontiming.c $(TESTDIR)/instructiontiming_asm.s $(CC65) $(MEGA65LIBC)
	$(CL65) -O -o $*.prg --mapfile $*.map $< $(TESTDIR)/instructiontiming_asm.s

$(TESTDIR)/floppytest.prg:       $(TESTDIR)/floppytest.c $(TESTDIR)/floppyread.s $(UTILDIR)/c65toc64wrapper.prg $(CC65) $(MEGA65LIBC)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o temp.prg --mapfile $*.map $< $(TESTDIR)/floppyread.s $(SRCDIR)/mega65-libc/cc65/src/memory.c $(SRCDIR)/mega65-libc/cc65/src/hal.c
	cat src/utilities/c65toc64wrapper.prg temp.prg > $*.prg

$(TESTDIR)/floppydrivetest.prg:       $(TESTDIR)/floppydrivetest.c $(TESTDIR)/floppyread.s $(UTILDIR)/c65toc64wrapper.prg $(CC65) $(MEGA65LIBC)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o temp.prg --mapfile $*.map $< $(TESTDIR)/floppyread.s $(SRCDIR)/mega65-libc/cc65/src/memory.c $(SRCDIR)/mega65-libc/cc65/src/hal.c
	cat src/utilities/c65toc64wrapper.prg temp.prg > $*.prg

$(TESTDIR)/floppycapacity.prg:       $(TESTDIR)/floppycapacity.c $(TESTDIR)/floppyread.s $(UTILDIR)/c65toc64wrapper.prg $(CC65) $(MEGA65LIBC)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o temp.prg --mapfile $*.map $< $(TESTDIR)/floppyread.s $(SRCDIR)/mega65-libc/cc65/src/memory.c $(SRCDIR)/mega65-libc/cc65/src/hal.c
	cat $(UTILDIR)/c65toc64wrapper.prg temp.prg > $*.prg

$(TESTDIR)/r3_production_test.prg:       $(TESTDIR)/r3_production_test.c $(CC65) $(MEGA65LIBC)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $< $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(TESTDIR)/ultrasoundtest.prg:       $(TESTDIR)/ultrasoundtest.c $(CC65) $(MEGA65LIBC)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $< $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(TESTDIR)/hyperramtest.prg:       $(TESTDIR)/hyperramtest.c $(CC65) $(MEGA65LIBC)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $< $(wildcard $(SRCDIR)/mega65-libc/cc65/src/*.c) $(wildcard $(SRCDIR)/mega65-libc/cc65/src/*.s)

##
## Examples
##
$(EXAMPLEDIR)/unicorns.prg:       $(EXAMPLEDIR)/unicorns.c $(CC65)
	$(CL65) -O -o $*.prg --mapfile $*.map $<

$(EXAMPLEDIR)/bmpview.prg:       $(EXAMPLEDIR)/bmpview.c $(CC65)
	$(CL65)  -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(EXAMPLEDIR)/modplay.prg:       $(EXAMPLEDIR)/modplay.c $(CC65)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(EXAMPLEDIR)/raycaster.prg:       $(EXAMPLEDIR)/raycaster.c $(CC65)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(EXAMPLEDIR)/verticalrasters.prg:	$(EXAMPLEDIR)/verticalrasters.asm $(ACME)
	$(ACME) --setpc 0x0801 --cpu m65 --format cbm --outfile $(EXAMPLEDIR)/verticalrasters.prg $(EXAMPLEDIR)/verticalrasters.asm

# is this still an example?
$(B65DIR)/wirekrill.prg:       $(EXAMPLEDIR)/wirekrill.c $(CC65)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(BINDIR)/wirekrill:	$(EXAMPLEDIR)/wirekrill.c
	$(CC) $(COPT) -DNATIVE_TEST -I/usr/local/include -L/usr/local/lib -o $(BINDIR)/wirekrill $(EXAMPLEDIR)/wirekrill.c -lpcap

##
## Unsorted Utilities
##
%.prg:	utilities/%.a65 $(OPHIS)
	$(info =============================================================)
	$(info ~~~~~~~~~~~~~~~~> Making: $@)
	$(OPHIS) $(OPHISOPT) utilities/$< -l $*.list -m $*.map -o $*.prg

$(UTILDIR)/gmod2-tools.prg:       $(UTILDIR)/gmod2-tools.c $(CC65)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(UTILDIR)/mega65_config.o:      $(UTILDIR)/mega65_config.s $(UTILDIR)/mega65_config.inc $(CC65)
	$(CA65) $< -l $*.list

$(UTILDIR)/mega65_config.prg:       $(UTILDIR)/mega65_config.o $(CC65)
	$(LD65) $< --mapfile $*.map -o $*.prg

$(UTILDIR)/megaflash.prg:       $(UTILDIR)/megaflash.c $(CC65) $(MEGA65LIBC)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(UTILDIR)/megaphonenorflash.prg:       $(UTILDIR)/megaphonenorflash.c $(CC65) $(MEGA65LIBC)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(UTILDIR)/c65toc64wrapper.prg:	$(UTILDIR)/c65toc64wrapper.asm $(ACME)
	$(ACME) --setpc 0x2001 --cpu m65 --format cbm --outfile $(UTILDIR)/c65toc64wrapper.prg $(UTILDIR)/c65toc64wrapper.asm

$(UTILDIR)/fastload_demo.prg:	$(UTILDIR)/fastload_demo.asm $(ACME)
	$(ACME) --setpc 0x0801 --cpu m65 --format cbm --outfile $(UTILDIR)/fastload_demo.prg $(UTILDIR)/fastload_demo.asm

$(UTILDIR)/fastload.prg:       $(UTILDIR)/fastload.c $(CC65) $(MEGA65LIBC)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(UTILDIR)/avtest.prg:       $(UTILDIR)/avtest.c $(CC65) $(MEGA65LIBC)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(UTILDIR)/i2clist.prg:       $(UTILDIR)/i2clist.c $(CC65)
	$(CL65) $< --mapfile $*.map -o $*.prg

$(UTILDIR)/i2cstatus.prg:       $(UTILDIR)/i2cstatus.c $(CC65) $(MEGA65LIBC)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(UTILDIR)/floppystatus.prg:       $(UTILDIR)/floppystatus.c $(CC65)
	$(CL65) $< --mapfile $*.map -o $*.prg

$(UTILDIR)/tiles.prg:       $(UTILDIR)/tiles.o $(CC65)
	$(LD65) $< --mapfile $*.map -o $*.prg

$(B65DIR)/b65support.bin:	$(UTILDIR)/b65support.a65 $(OPHIS)
	$(OPHIS) $(OPHISOPT) $< -l $*.list -m $*.map -o $*.bin

$(B65DIR)/ethertest.prg:	$(UTILDIR)/ethertest.a65 $(OPHIS)
	$(OPHIS) $(OPHISOPT) $< -l $*.list -m $*.map -o $*.prg

$(B65DIR)/cartload.prg:       $(UTILDIR)/cartload.c $(CC65) $(MEGA65LIBC)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/memory.c

$(B65DIR)/rompatch.prg:       $(UTILDIR)/rompatch.c $(CC65) $(MEGA65LIBC)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --mapfile $*.map $<  $(SRCDIR)/mega65-libc/cc65/src/*.c $(SRCDIR)/mega65-libc/cc65/src/*.s

$(B65DIR)/etherload.prg:	$(UTILDIR)/etherload.a65 $(OPHIS)
	$(OPHIS) $(OPHISOPT) $< -l $*.list -m $*.map -o $*.prg

$(TOOLDIR)/ascii_font.c:	$(BINDIR)/asciifont.bin
	echo "unsigned char ascii_font[4097]={" > $(TOOLDIR)/ascii_font.c
	hexdump -v -e '/1 "0x%02X, "' -e '8/1 """\n"' $(BINDIR)/asciifont.bin >> $(TOOLDIR)/ascii_font.c
	echo "0};" >>$(TOOLDIR)/ascii_font.c

$(BINDIR)/asciifont.bin:	$(BINDIR)/pngprepare $(ASSETS)/ascii00-ff.png
	$(BINDIR)/pngprepare charrom $(ASSETS)/ascii00-ff.png $(BINDIR)/asciifont.bin

$(BINDIR)/charrom.bin:	$(BINDIR)/pngprepare $(ASSETS)/8x8font.png
	$(BINDIR)/pngprepare charrom $(ASSETS)/8x8font.png $(BINDIR)/charrom.bin

# c-code that makes an executable that processes images, and can make a vhdl file
$(BINDIR)/pngprepare:	$(TOOLDIR)/pngprepare/pngprepare.c Makefile
	$(CC) $(COPT) -I/usr/local/include -L/usr/local/lib -o $(BINDIR)/pngprepare $(TOOLDIR)/pngprepare/pngprepare.c -lpng

$(BINDIR)/giftotiles:	$(TOOLDIR)/pngprepare/giftotiles.c Makefile
	$(CC) $(COPT) -I/usr/local/include -L/usr/local/lib -o $(BINDIR)/giftotiles $(TOOLDIR)/pngprepare/giftotiles.c -lgif

# Utility to make hi-colour displays from PNGs, with upto 256 colours per char row
$(BINDIR)/pnghcprepare:	$(TOOLDIR)/pngprepare/pnghcprepare.c Makefile
	$(CC) $(COPT) -I/usr/local/include -L/usr/local/lib -o $(BINDIR)/pnghcprepare $(TOOLDIR)/pngprepare/pnghcprepare.c -lpng

# Utility to make prerendered H65 pages from markdopwn source files
$(BINDIR)/md2h65:	$(TOOLDIR)/pngprepare/md2h65.c Makefile $(TOOLDIR)/ascii_font.c
	$(CC) $(COPT) -I/usr/local/include -I/usr/include/freetype2 -L/usr/local/lib -o $(BINDIR)/md2h65 $(TOOLDIR)/pngprepare/md2h65.c $(TOOLDIR)/ascii_font.c -lpng -lfreetype

$(BINDIR)/utilpacker:	$(BINDIR)/utilpacker.c Makefile
	$(CC) $(COPT) -o $(BINDIR)/utilpacker $(TOOLDIR)/utilpacker/utilpacker.c

$(SDCARD_DIR)/BANNER.M65:	$(BINDIR)/pngprepare $(ASSETS)/mega65_320x64.png /usr/bin/convert
	/usr/bin/convert -colors 128 -depth 8 +dither $(ASSETS)/mega65_320x64.png $(BINDIR)/mega65_320x64_128colour.png
	$(BINDIR)/pngprepare logo $(BINDIR)/mega65_320x64_128colour.png $(SDCARD_DIR)/BANNER.M65

##
## Unsorted Tools
##
$(TOOLDIR)/merge-issue:	$(TOOLDIR)/merge-issue.c
	$(CC) $(COPT) -o $(TOOLDIR)/merge-issue $(TOOLDIR)/merge-issue.c

$(TOOLDIR)/vhdl-path-finder:	$(TOOLDIR)/vhdl-path-finder.c
	$(CC) $(COPT) -o $(TOOLDIR)/vhdl-path-finder $(TOOLDIR)/vhdl-path-finder.c

$(TOOLDIR)/osk_image:	$(TOOLDIR)/osk_image.c
	$(CC) $(COPT) -I/usr/local/include -L/usr/local/lib -o $(TOOLDIR)/osk_image $(TOOLDIR)/osk_image.c -lpng

$(TOOLDIR)/frame2png:	$(TOOLDIR)/frame2png.c
	$(CC) $(COPT) -I/usr/local/include -L/usr/local/lib -o $(TOOLDIR)/frame2png $(TOOLDIR)/frame2png.c -lpng

$(BINDIR)/ethermon:	$(TOOLDIR)/ethermon.c
	$(CC) $(COPT) -o $(BINDIR)/ethermon $(TOOLDIR)/ethermon.c -I/usr/local/include -lpcap

$(BINDIR)/etherload:	$(TOOLDIR)/etherload/etherload.c
	$(CC) $(COPT) -o $(BINDIR)/etherload $(TOOLDIR)/etherload/etherload.c -I/usr/local/include

$(BINDIR)/videoproxy:	$(TOOLDIR)/videoproxy.c
	$(CC) $(COPT) -o $(BINDIR)/videoproxy $(TOOLDIR)/videoproxy.c -I/usr/local/include -lpcap

$(BINDIR)/vncserver:	$(TOOLDIR)/vncserver.c
	$(CC) $(COPT) -O3 -o $(BINDIR)/vncserver $(TOOLDIR)/vncserver.c -I/usr/local/include -lvncserver -lpthread

$(BINDIR)/mfm-decode:	$(TOOLDIR)/mfm-decode.c
	$(CC) $(COPT) -g -Wall -o $(BINDIR)/mfm-decode $(TOOLDIR)/mfm-decode.c

$(BINDIR)/trenzm65powercontrol:	$(TOOLDIR)/trenzm65powercontrol.c $(TOOLDIR)/m65common.c $(TOOLDIR)/logging.c $(TOOLDIR)/version.c include/*.h Makefile
	$(CC) $(COPT) -g -Wall -Iinclude $(LIBUSBINC) -o $(BINDIR)/trenzm65powercontrol $(TOOLDIR)/trenzm65powercontrol.c $(TOOLDIR)/m65common.c $(TOOLDIR)/logging.c $(TOOLDIR)/version.c -lusb-1.0 -lz -lpthread -lpng

$(BINDIR)/readdisk:	$(TOOLDIR)/readdisk.c $(TOOLDIR)/m65common.c $(TOOLDIR)/logging.c $(TOOLDIR)/version.c $(TOOLDIR)/screen_shot.c $(TOOLDIR)/fpgajtag/*.c $(TOOLDIR)/fpgajtag/*.h include/*.h Makefile
	$(CC) $(COPT) -g -Wall -Iinclude $(LIBUSBINC) -o $(BINDIR)/readdisk $(TOOLDIR)/readdisk.c $(TOOLDIR)/m65common.c $(TOOLDIR)/logging.c $(TOOLDIR)/version.c $(TOOLDIR)/fpgajtag/fpgajtag.c $(TOOLDIR)/fpgajtag/util.c $(TOOLDIR)/fpgajtag/usbserial.c $(TOOLDIR)/fpgajtag/process.c -lusb-1.0 -lz -lpthread -lpng

# Create targets for binary (linux) and binary.exe (mingw) easily, minimising repetition
# arg1 = target name (without .exe)
# arg2 = pre-requisites
define TRIPLE_TARGET
$(1): $(2) $(TOOLDIR)/version.c Makefile
	$$(CC) -g -Wall -Iinclude -o $$@ $$(filter %.c,$$^)

$(1).exe: $(2) $(TOOLDIR)/version.c Makefile
	$$(WINCC) $$(WINCOPT) -g -Wall -Iinclude -o $$@ $$(filter %.c,$$^)

$(1)_intel.osx: $(2) $(TOOLDIR)/version.c Makefile
	$(CC) $(MACINTELCOPT) -Iinclude -o $$@ $$(filter %.c,$$^)
$(1)_arm.osx: $(2) $(TOOLDIR)/version.c Makefile
	$(CC) $(MACARMCOPT) -Iinclude -o $$@ $$(filter %.c,$$^)
endef

# Creates 2 targets:
# - bin/bit2core (for linux)
# - bin/bit2core.exe (for mingw)
$(eval $(call TRIPLE_TARGET, $(BINDIR)/bit2core, $(TOOLDIR)/bit2core.c))

$(eval $(call TRIPLE_TARGET, $(BINDIR)/bit2mcs, $(TOOLDIR)/bit2mcs.c))

$(eval $(call TRIPLE_TARGET, $(BINDIR)/romdiff, $(TOOLDIR)/romdiff.c))

##
## ========== m65 ==========
##
M65_SRC= $(TOOLDIR)/m65.c \
         $(TOOLDIR)/m65common.c \
	 $(TOOLDIR)/logging.c \
	 $(TOOLDIR)/version.c \
	 $(TOOLDIR)/screen_shot.c \
	 $(TOOLDIR)/fpgajtag/fpgajtag.c \
	 $(TOOLDIR)/fpgajtag/util.c \
	 $(TOOLDIR)/fpgajtag/usbserial.c \
	 $(TOOLDIR)/fpgajtag/process.c

$(BINDIR)/m65:	$(M65_SRC) $(TOOLDIR)/fpgajtag/*.h include/*.h Makefile
	$(CC) $(COPT) -Iinclude $(LIBUSBINC) -o $@ $(M65_SRC) -lusb-1.0 -lz -lpthread -lpng

$(BINDIR)/m65_intel.osx:	$(M65_SRC) $(TOOLDIR)/fpgajtag/*.h include/*.h Makefile
	$(CC) $(MACINTELCOPT) -Iinclude -o $@ $(M65_SRC) -framework Security

$(BINDIR)/m65_arm.osx:	$(M65_SRC) $(TOOLDIR)/fpgajtag/*.h include/*.h Makefile
	$(CC) $(MACARMCOPT) -Iinclude -o $@ $(M65_SRC) -framework Security

$(BINDIR)/m65.exe:	$(M65_SRC) $(TOOLDIR)/fpgajtag/*.h include/*.h Makefile
	$(WINCC) $(WINCOPT) -D_FORTIFY_SOURCES=2 -Iinclude $(LIBUSBINC) -I$(TOOLDIR)/fpgajtag/ -o $@ $(M65_SRC) -Wl,-Bstatic -lusb-1.0 -lwsock32 -lws2_32 -lpng -lz -lssp -Wl,-Bdynamic
# $(TOOLDIR)/fpgajtag/listComPorts.c $(TOOLDIR)/fpgajtag/disphelper.c

## special target for linux static win build even if DO_STATIC is 0
static_m65_exe:		$(M65_SRC) $(TOOLDIR)/fpgajtag/*.c $(TOOLDIR)/fpgajtag/*.h $(TOOLDIR)/version.c Makefile
	$(WINCC) $(WINCOPT) -D_FORTIFY_SOURCES=2 -Iinclude $(LIBUSBINC) -I$(TOOLDIR)/fpgajtag/ -o $@ $(M65_SRC) -Wl,-Bstatic -lusb-1.0 -lwsock32 -lws2_32 -lpng -lz -lssp -Wl,-Bdynamic
##
## ========== mega65_ftp ==========
##
$(LIBEXECDIR)/ftphelper.bin:	$(TOOLDIR)/ftphelper.a65
	$(OPHIS) $(OPHISOPT) $(TOOLDIR)/ftphelper.a65

$(UTILDIR)/remotesd.prg:       $(UTILDIR)/remotesd.c $(CC65) $(MEGA65LIBC)
	$(CL65) -I $(SRCDIR)/mega65-libc/cc65/include -O -o $*.prg --listing $*.list --mapfile $*.map --add-source $<  $(SRCDIR)/mega65-libc/cc65/src/memory.c

$(TOOLDIR)/ftphelper.c:	$(UTILDIR)/remotesd.prg $(TOOLDIR)/bin2c
	$(TOOLDIR)/bin2c $(UTILDIR)/remotesd.prg helperroutine $(TOOLDIR)/ftphelper.c

define LINUX_AND_MINGW_GTEST_TARGETS
$(1): $(2)
	$$(CXX) $$(COPT) $$(GTESTOPTS) -Iinclude $(LIBUSBINC) -o $$@ $$(filter %.c %.cpp,$$^) $(TOOLDIR)/version.c -lreadline -lncurses -lgtest_main -lgtest -lpthread $(3)

$(1).exe: $(2)
	$$(CXX) $$(WINCOPT) $$(GTESTOPTS) -Iinclude $(LIBUSBINC) -o $$@ $$(filter %.c %.cpp,$$^) $(TOOLDIR)/version.c -lreadline -lncurses -lgtest_main -lgtest -lpthread $$(BUILD_STATIC) -lwsock32 -lws2_32 -lz -Wl,-Bdynamic $(3)
endef

MEGA65FTP_SRC=	$(TOOLDIR)/mega65_ftp.c \
		$(TOOLDIR)/m65common.c \
		$(TOOLDIR)/logging.c \
		$(TOOLDIR)/ftphelper.c \
		$(TOOLDIR)/filehost.c \
		$(TOOLDIR)/diskman.c \
		$(TOOLDIR)/bit2mcs.c

# Gives two targets of:
# - gtest/bin/mega65_ftp.test
# - gtest/bin/mega65_ftp.test.exe
$(eval $(call LINUX_AND_MINGW_GTEST_TARGETS, $(GTESTBINDIR)/mega65_ftp.test, $(GTESTDIR)/mega65_ftp_test.cpp $(MEGA65FTP_SRC) Makefile, -DINCLUDE_BIT2MCS -fpermissive))

# Gives two targets of:
# - gtest/bin/bit2core.test
# - gtest/bin/bit2core.test.exe
$(eval $(call LINUX_AND_MINGW_GTEST_TARGETS, $(GTESTBINDIR)/bit2core.test, $(GTESTDIR)/bit2core_test.cpp $(TOOLDIR)/bit2core.c Makefile, -fpermissive))

$(BINDIR)/mega65_ftp: $(MEGA65FTP_SRC) $(TOOLDIR)/version.c include/*.h Makefile
	$(CC) $(COPT) -D_FILE_OFFSET_BITS=64 -Iinclude $(LIBUSBINC) -o $(BINDIR)/mega65_ftp $(MEGA65FTP_SRC) $(TOOLDIR)/version.c $(BUILD_STATIC) -lreadline -lncurses -ltinfo -Wl,-Bdynamic -DINCLUDE_BIT2MCS

$(BINDIR)/mega65_ftp.static: $(MEGA65FTP_SRC) $(TOOLDIR)/version.c include/*.h Makefile ncurses/lib/libncurses.a readline/libreadline.a readline/libhistory.a
	$(CC) $(COPT) -Iinclude $(LIBUSBINC) -mno-sse3 -o $(BINDIR)/mega65_ftp.static $(MEGA65FTP_SRC) $(TOOLDIR)/version.c ncurses/lib/libncurses.a readline/libreadline.a readline/libhistory.a -ltermcap -DINCLUDE_BIT2MCS

$(BINDIR)/mega65_ftp.exe: $(MEGA65FTP_SRC) $(TOOLDIR)/version.c include/*.h Makefile
	$(WINCC) $(WINCOPT) -D_FILE_OFFSET_BITS=64 -g -Wall -Iinclude $(LIBUSBINC) -I$(TOOLDIR)/fpgajtag/ -o $(BINDIR)/mega65_ftp.exe $(MEGA65FTP_SRC) $(TOOLDIR)/version.c -lusb-1.0 $(BUILD_STATIC) -lwsock32 -lws2_32 -lz -Wl,-Bdynamic -DINCLUDE_BIT2MCS

$(BINDIR)/mega65_ftp_intel.osx: $(MEGA65FTP_SRC) $(TOOLDIR)/version.c include/*.h Makefile
	$(CC) $(MACINTELCOPT) -D__APPLE__ -D_FILE_OFFSET_BITS=64 -o $@ -Iinclude $(MEGA65FTP_SRC) $(TOOLDIR)/version.c -lpthread -lreadline -DINCLUDE_BIT2MCS

$(BINDIR)/mega65_ftp_arm.osx: $(MEGA65FTP_SRC) $(TOOLDIR)/version.c include/*.h Makefile
	$(CC) $(MACARMCOPT) -D__APPLE__ -D_FILE_OFFSET_BITS=64 -o $@ -Iinclude $(MEGA65FTP_SRC) $(TOOLDIR)/version.c -lpthread -lreadline -DINCLUDE_BIT2MCS

$(BINDIR)/bitinfo:	$(TOOLDIR)/bitinfo.c Makefile
	$(CC) $(COPT) -g -Wall -o $(BINDIR)/bitinfo $(TOOLDIR)/bitinfo.c

$(BINDIR)/m65ftp_test:	$(TESTDIR)/m65ftp_test.c
	$(CC) $(COPT) -g -Wall -o $(BINDIR)/m65ftp_test $(TESTDIR)/m65ftp_test.c

##
## ========== m65dbg ==========
##
TEST:=$(shell test -d /cygdrive && echo cygwin)
ifneq "$(TEST)" ""
  M65DEBUG_READLINE=-L/usr/bin -lreadline7
else
  M65DEBUG_READLINE=-lreadline
endif

M65DBG_SOURCES = $(TOOLDIR)/m65dbg/m65dbg.c $(TOOLDIR)/m65dbg/commands.c $(TOOLDIR)/m65dbg/gs4510.c $(TOOLDIR)/m65dbg/serial.c $(TOOLDIR)/logging.c $(TOOLDIR)/m65common.c $(TOOLDIR)/screen_shot.c $(TOOLDIR)/fpgajtag/usbserial.c $(TOOLDIR)/version.c
M65DBG_INCLUDES = -Iinclude $(LIBUSBINC)
M65DBG_LIBRARIES = -lpng -lpthread -lusb-1.0 -lz $(M65DEBUG_READLINE)

$(BINDIR)/m65dbg:	$(M65DBG_SOURCES) $(M65DBG_HEADERS) Makefile
	$(CC) $(COPT) $(M65DBG_INCLUDES) -o $@ $(M65DBG_SOURCES) $(M65DBG_LIBRARIES)

$(BINDIR)/m65dbg_intel.osx:	$(M65DBG_SOURCES) $(M65DBG_HEADERS) Makefile
	$(CC) $(MACINTELCOPT) -Iinclude -o $@ $(M65DBG_SOURCES) $(M65DEBUG_READLINE)

$(BINDIR)/m65dbg_arm.osx:	$(M65DBG_SOURCES) $(M65DBG_HEADERS) Makefile
	$(CC) $(MACARMCOPT) -Iinclude -o $@ $(M65DBG_SOURCES) $(M65DEBUG_READLINE)

$(BINDIR)/m65dbg.exe:	$(M65DBG_SOURCES) $(M65DBG_HEADERS) Makefile
	$(WINCC) $(WINCOPT) $(M65DBG_INCLUDES) -o $@ $(M65DBG_SOURCES) $(M65DBG_LIBRARIES) $(BUILD_STATIC) -lwsock32 -lws2_32 -Wl,-Bdynamic

#-----------------------------------------------------------------------------
