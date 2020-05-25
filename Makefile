DEBUG_CFLAGS=-O0 -g -Wall -Wextra -Wconversion -Wpedantic -pedantic -std=gnu11
midi: main.c
	$(CC) $(DEBUG_CFLAGS) -o midi main.c
