CFLAGS ?= -g -O3 -Wall

all: ost

ost: ost.c ost.h ruring.o
	gcc -o ost ost.c ruring.o -luring ${CFLAGS}

ruring.o: ruring.c ruring.h
	gcc -c ruring.c -luring ${CFLAGS}
