CC = gcc
CFLAGS = -Wall -I.

all: gauge

gauge: i2c_impl.c gauge.c gauge.h
	$(CC) $(CFLAGS) -o gauge i2c_impl.c gauge.c

clean:
	rm -f gauge
