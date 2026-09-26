# GBHV User-Mode Tools - MSYS2 UCRT64 Makefile
# Build with: make (from MSYS2 UCRT64 shell)

CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lole32 -lshell32

all: setup.exe hvlog.exe

setup.exe: setup.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

hvlog.exe: hvlog.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f setup.exe hvlog.exe
