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

PROJECT_ROOT= ..
SRC=$(PROJECT_ROOT)/src
RUN=$(PROJECT_ROOT)/build
OPT = -Os
INCLUDE = -I $(SRC)
HEADERS = $(SRC)/controller.h $(SRC)/cpu.h $(SRC)/flow_table.h $(SRC)/isa.h $(SRC)/match_in.h $(SRC)/port.h $(SRC)/queue.h $(SRC)/send_out.h \
          $(SRC)/send_ring.h  $(SRC)/tick.h $(SRC)/thread.h
OBJECTS= controller.o cpu.o flow_table.o isa.o match_in.o port.o queue.o send_out.o send_ring.o tick.o thread.o
CFLAGS = -std=gnu99 -Wall -g  -pthread
LDFLAGS = $(if $(filter -m32, $(XFLAGS)), -static) -pthread
LIBS = -lgxio -ltmc

$(RUN)/switch: main.o $(OBJECTS)
	$(CC) $(INCLUDE) $(LDFLAGS) $(XFLAGS) -o $@ $^  $(LIBS)

flow_table.o: $(SRC)/flow_table.c $(HEADERS)
	$(CC) $(INCLUDE) $(CFLAGS) $(XFLAGS) -c -o $@ $< -DGXIO_MULTI_MPIPES

main.o: $(SRC)/main.c $(HEADERS)
	$(CC) $(INCLUDE) $(CFLAGS) $(XFLAGS) -c -o $@ $< -DGXIO_MULTI_MPIPES

port.o: $(SRC)/port.c $(HEADERS)
	$(CC) $(INCLUDE) $(CFLAGS) $(XFLAGS) -c -o $@ $< -DGXIO_MULTI_MPIPES

cpu.o: $(SRC)/cpu.c $(HEADERS)
	$(CC) $(INCLUDE) $(CFLAGS) $(XFLAGS) -c -o $@ $< -DGXIO_MULTI_MPIPES

controller.o: $(SRC)/controller.c $(HEADERS)
	$(CC) $(INCLUDE) $(CFLAGS) $(XFLAGS) -c -o $@ $< -DGXIO_MULTI_MPIPES

match_in.o: $(SRC)/match_in.c $(HEADERS)
	$(CC) $(INCLUDE) $(CFLAGS) $(XFLAGS) -c -o $@ $< -DGXIO_MULTI_MPIPES

send_ring.o: $(SRC)/send_ring.c $(HEADERS)
	$(CC) $(INCLUDE) $(CFLAGS) $(XFLAGS) -c -o $@ $< -DGXIO_MULTI_MPIPES

send_out.o: $(SRC)/send_out.c $(HEADERS)
	$(CC) $(INCLUDE) $(CFLAGS) $(XFLAGS) -c -o $@ $< -DGXIO_MULTI_MPIPES

queue.o: $(SRC)/queue.c $(HEADERS)
	$(CC) $(INCLUDE) $(CFLAGS) $(XFLAGS) -c -o $@ $< -DGXIO_MULTI_MPIPES

isa.o: $(SRC)/isa.c $(HEADERS)
	$(CC) $(INCLUDE) $(CFLAGS) $(XFLAGS) -c -o $@ $< -DGXIO_MULTI_MPIPES

tick.o: $(SRC)/tick.c $(HEADERS)
	$(CC) $(INCLUDE) $(CFLAGS) $(XFLAGS) -c -o $@ $< -DGXIO_MULTI_MPIPES

thread.o: $(SRC)/thread.c $(HEADERS)
	$(CC) $(INCLUDE) $(CFLAGS) $(XFLAGS) -c -o $@ $< -DGXIO_MULTI_MPIPES

clean:
	rm -f *.o switch 

.PHONY: all clean
