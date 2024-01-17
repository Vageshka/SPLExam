CC=gcc
CFLAGS=-pthread
LDFLAGS=
SOURCES=client.c server.c
OBJECTS=$(SOURCES:.c=.o)

all: clean build

build: $(OBJECTS)

.SUFFIXES: .c .o

.c.o:
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(OBJECTS)



