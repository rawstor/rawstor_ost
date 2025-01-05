CFLAGS ?= -g -O2 -Wall

all:
	gcc ost.c -luring -o ost ${CFLAGS}
