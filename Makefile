CC = gcc
CFLAGS = -I. -ggdb -fopenmp -Wall -Werror -Wextra -pedantic

all: bnc

bnc: %: %.o
	$(CC) $(CFLAGS) -o $* $<
