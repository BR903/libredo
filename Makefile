# Makefile for libredo.

CC = gcc
CFLAGS = -Wall -Wextra -O2
VERSION = 0.7


all: libredo.a test sokoban-example


libredo.a: redo.o
	ar crs $@ $^

redo.o: redo.c redo.h


test: redo-tests
	./redo-tests

redo-tests: redo-tests.o libredo.a
redo-tests.o: redo-tests.c redo.h


sokoban-example: sokoban-example.o libredo.a
	$(CC) -o $@ $^ -lncurses

sokoban-example.o: sokoban-example.c redo.h


clean:
	rm -f libredo.a redo-tests sokoban-example
	rm -f redo.o redo-tests.o sokoban-example.o


dist:
	rm -f libredo-$(VERSION).tar.gz
	mkdir libredo-$(VERSION)
	cp -a `cat MANIFEST` libredo-$(VERSION)/
	tar -czf libredo-$(VERSION).tar.gz libredo-$(VERSION)
	rm -r libredo-$(VERSION)
