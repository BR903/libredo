# Makefile for libredo.
#
# The list of symbolic targets:
#
# make all      = build the library, unit tests, and sample program
# make check    = build and run the unit tests
# make clean    = delete files created by the build process
# make dist     = create the source distribution package

prefix = /usr/local
exec_prefix = ${prefix}
libdir = $(DESTDIR)${exec_prefix}/lib

# The library's version number.
#
VERSION = 0.8

# Compiler configuration.
#
CC = gcc
CFLAGS = -Wall -Wextra

# Library archiver configuration.
#
AR = ar
ARFLAGS = crs

#
# Build rules for the library, and the accompanying programs.
#

libredo.a: redo.o
	$(AR) $(ARFLAGS) $@ $^

redo.o: redo.c redo.h types.h

redo-tests: redo-tests.o libredo.a
redo-tests.o: redo-tests.c redo.h types.h

sokoban-example: sokoban-example.o libredo.a
	$(CC) -o $@ $^ -lncurses

sokoban-example.o: sokoban-example.c redo.h types.h

#
# Symbolic build targets.
#

.PHONY: all check clean dist

all: libredo.a check sokoban-example

check: redo-tests
	./redo-tests

clean:
	rm -f libredo.a redo-tests sokoban-example
	rm -f redo.o redo-tests.o sokoban-example.o

dist:
	rm -f libredo-$(VERSION).tar.gz
	mkdir libredo-$(VERSION)
	cp -a $(shell cat MANIFEST) libredo-$(VERSION)/
	tar -czf libredo-$(VERSION).tar.gz libredo-$(VERSION)
	rm -r libredo-$(VERSION)
