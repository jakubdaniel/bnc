CC = gcc
CFLAGS = -I. -ggdb -fopenmp -Wall -Wextra -Werror -pedantic -pthread

all: bnc

bnc: %: %.o
	$(CC) $(CFLAGS) -o $* $<
