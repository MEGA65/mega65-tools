#!/usr/bin/python3

#######################################################################
# png2fci version 1.0                                                 #
# written by stephan kleinert @ hundehaus im reinhardswald, june 2021 #
# with very special thanks to frau k., buba k. and candor k.!         #
#######################################################################

import sys
from zlib import compress
import png
import io

gVerbose = False
gReserve = False
gCompress = False
gExcludePalette = False
gVersion = "1.0"
gPreserveBackgroundColour = False


def vprint(*values):
    global gVerbose
    if gVerbose == True:
        print(*values)


def showUsage():
    print("usage: "+sys.argv[0]+" [-rv] infile outfile")
    print("convert PNG to MEGA65 FCI file")
    print("options: -r  reserve system palette entries")
    print("         -0  keep palette entry 0")
    print("         -x  exclude palette data")
    print("         -v  verbose output")
    print("         -c  compress output")
    exit(0)


def nybswap(i):
    lownyb = i % 16
    hinyb = i // 16
    out = (lownyb*16)+hinyb
    return out


def parseArgs():
    global gReserve, gVerbose, gCompress, gExcludePalette, gPreserveBackgroundColour
    args = sys.argv.copy()
    args.remove(args[0])
    fileargcount = 0

    for arg in args:
        if arg[0:1] == "-":
            opts = arg[1:]
            for opt in opts:
                if opt == "r":
                    gReserve = True
                elif opt == "v":
                    gVerbose = True
                elif opt == "c":
                    gCompress = True
                elif opt == "x":
                    gExcludePalette = True
                elif opt == "0":
                    gPreserveBackgroundColour = True

                else:
                    print("Unknown option", opt)
                    showUsage()
        else:
            fileargcount += 1
            if fileargcount == 1:
                infile = arg
            elif fileargcount == 2:
                outfile = arg
            else:
                print("too many arguments")
                showUsage()
    if fileargcount != 2:
        showUsage()
    return infile, outfile


def pngRowsToM65Rows(pngRows):
    pngX = 0
    pngY = 0
    height = len(pngRows)
    width = len(pngRows[0])
    columnCount = width//8
    rowCount = height//8
    vprint("using", rowCount, "rows,", columnCount, "columns.")
    m65Rows = []
    for i in range(rowCount):
        aRow = []
        for b in range(columnCount):
            aChar = bytearray()
            for c in range(64):
                aChar.append(0)
            aRow.append(aChar)
        m65Rows.append(aRow)
    for currentRow in pngRows:
        pngX = 0
        for currentColumn in currentRow:
            if gReserve:
                if gPreserveBackgroundColour:
                    if currentColumn != 0:
                        currentColumn += 16
                else:
                    currentColumn += 16
            m65X = pngX//8
            m65Y = pngY//8
            m65Byte = ((pngX % 8)+(pngY*8)) % 64
            m65Rows[m65Y][m65X][m65Byte] = currentColumn
            pngX += 1
        pngY += 1

    imageData = bytearray()
    for currentRow in m65Rows:
        for currentColumn in currentRow:
            imageData.extend(currentColumn)
    return imageData, rowCount, columnCount


def rle(data):
    outdata = []
    dsize = len(data)
    i = 0
    while i < dsize:
        current = data[i]
        count = 1
        if i < dsize:
            j = i+1
            while (j < dsize-1) and (data[j] == current and (count < 255)):
                count += 1
                j += 1
        if count == 1:
            outdata.append(current)
            i += 1
        else:
            outdata.append(current)
            outdata.append(current)
            outdata.append(count)
            i = j
    # print(outdata)
    return outdata


####################### main program ########################

# print(rle(["a", "b", "c", "c", "c", "d", "e"]))
# exit(0)

inputFileName, outputFileName = parseArgs()

vprint("### png2fci v"+gVersion+" ###")
vprint("reading", inputFileName)
pngReader = png.Reader(filename=inputFileName)
pngData = pngReader.read()
pngInfo = pngData[3]

gWidth = pngInfo["size"][0]
gHeight = pngInfo["size"][1]

vprint("infile size is ", gWidth, "x", gHeight, "pixels")

if (gWidth % 8 != 0 or gHeight % 8 != 0):
    print("error: widht and height must be multiple of 8,")
    print("but actual dimensions are ", gWidth, "x", gHeight)
    exit(5)


try:
    palette = pngInfo["palette"]
except:
    print("error: infile has no palette")
    exit(1)


vic4_palette = []

if gExcludePalette:
    vprint("excluding palette data")
else:
    if gReserve:
        if len(palette) > 240:
            print("error: can't reserve system palette because source PNG "
                  "has >240 palette entries.")
            exit(2)

        # add placeholders for system colours
        vprint("reserving system colour space")
        if gPreserveBackgroundColour:
            vprint("preserve palette entry 0")
            maxC = 15
        else:
            maxC = 16
        for i in range(maxC):
            vic4_palette.append((0, 0, 0))

    for i in palette:
        vic4_palette.append((i[0], i[1], i[2]))

    vprint("outfile has", len(vic4_palette), "palette entries")

rows = list(pngData[2])
imageData, numRows, numColumns = pngRowsToM65Rows(rows)
m65data = bytearray()

vprint("building outfile")
m65data.extend(map(ord, 'fciP'))  # 0-3 : identifier bytes for format
m65data.append(0x01)  # 4 : version
m65data.append(numRows)  # 5 : number of rows
m65data.append(numColumns)  # 6 : number of columns
# 7 : options (b0: RLE compressed; b1: sys palette reserved)
m65data.append(gCompress+(2*gReserve))
m65data.append(len(vic4_palette)-1)  # 8 : palette size

if not gExcludePalette:
    for entry in vic4_palette:
        m65data.extend(entry)

m65data.extend(map(ord, 'IMG'))

if gCompress:
    m65data.extend(rle(imageData))
else:
    m65data.extend(imageData)

outfile = open(outputFileName, "wb")
outfile.write(m65data)
vprint("done.")
outfile.close()
