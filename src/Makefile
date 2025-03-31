CFLAGS ?= -g -O3 -Wall

all: ost

ost: ost.c ost.h ruring.o uuid.o
	gcc -o ost ost.c ruring.o uuid.o -luring ${CFLAGS}

ruring.o: ruring.c ruring.h
	gcc -c ruring.c -luring ${CFLAGS}

uuid.o: uuid.c uuid.h
	gcc -c uuid.c ${CFLAGS}
