# Refreshed my memory on how to write makefiles via this site:
# - http://mrbook.org/blog/tutorials/make/

CC=gcc
CFLAGS=-c -Wall -g
LDFLAGS=-L/usr/bin -lreadline7
SOURCES=main.c serial.c commands.c
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=m65dbg

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJECTS) $(EXECUTABLE)
