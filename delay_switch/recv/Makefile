# Default target.
all:

ifdef TILERA_ROOT

CC = $(TILERA_ROOT)/bin/tile-gcc
MPIPE_CC = $(TILERA_ROOT)/bin/tile-mpipe-cc

else

ifeq ($(filter tile%,$(shell uname -m)),)
$(error The 'TILERA_ROOT' environment variable is not set.)
endif

CC = gcc
MPIPE_CC = tile-mpipe-cc

endif


OPT = -Os
CFLAGS = -std=gnu99 -Wall -Wshadow -g $(OPT)
LDFLAGS = $(if $(filter -m32, $(XFLAGS)), -static)
LIBS = -lgxio -ltmc

app.o: recv.c
	$(CC) $(CFLAGS) $(XFLAGS) -c -o $@ $<

recv: app.o
	$(CC) $(LDFLAGS) $(XFLAGS) -o $@ $< $(LIBS)

all: recv

clean:
	rm -f *.o recv

.PHONY: all clean
