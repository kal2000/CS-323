CC=gcc
CFLAGS=-g3 -std=c99 -pedantic -Wall
HWK=/c/cs323/Hwk4

all: Parse

clean:
	$(RM) Parse *.o

tokenize.o: getLine.h parse.h
mainParse.o: getLine.h parse.h
parse.o: parse.h
getLine.o: getLine.h

Parse: mainParse.o parse.o getLine.o tokenize.o
	$(CC) $(CFLAGS) -o $@ $^

