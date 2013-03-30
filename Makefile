CC = gcc
CFLAGS = -I. -ggdb

all: bnc

bnc: %: %.o
	$(CC) -o $* $<
