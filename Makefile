OTHER_CFLAGS=-Wall -Wextra -Wconversion -Wno-unused-function -Wpedantic -pedantic -std=gnu11 -lm -lasound -pthread
DEBUG_CFLAGS=-O0 -g $(OTHER_CFLAGS)
RELEASE_CFLAGS=-O3 -s $(OTHER_CFLAGS)
midi: *.[ch]
	$(CC) $(DEBUG_CFLAGS) -o midi main.c
midi_release: *.[ch]
	$(CC) $(RELEASE_CFLAGS) -o midi main.c
