CC=gcc
CFLAGS=-g -std=c99 -pedantic -Wall

all: Far
Far: far.o
	$(CC) $(CFLAGS) -o $@ $^

clean:
	$(RM) Far *.o

