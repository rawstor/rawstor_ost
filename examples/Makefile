IDIR =../src/

CFLAGS ?= -g -O3 -Wall -I${IDIR}

all:
	gcc ost_client.c ../src/uuid.o -luring -o ost-client ${CFLAGS}
