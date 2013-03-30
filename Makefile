CC = gcc
CFLAGS = -I. -ggdb -fopenmp -Wall -Werror -pedantic

all: bnc

bnc: %: %.o
	$(CC) $(CFLAGS) -o $* $<
