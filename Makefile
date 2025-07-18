# Makefile for multicast test programs

CC = gcc
CFLAGS = -Wall -O2

TARGETS = multicast multicast6

all: $(TARGETS)

multicast: multicast.c
	$(CC) $(CFLAGS) -o $@ $<

multicast6: multicast6.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS)
