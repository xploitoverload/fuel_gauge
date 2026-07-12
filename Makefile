CC = gcc
CFLAGS = -Wall -I.

all: gauge_test

gauge_test: i2c_impl.c gauge.c gauge.h
	$(CC) $(CFLAGS) -o gauge_test i2c_impl.c gauge.c

clean:
	rm -f gauge_test
