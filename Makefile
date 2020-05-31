OTHER_CFLAGS=-Wall -Wextra -Wconversion -Wshadow -Wno-unused-function -Wpedantic -pedantic -std=gnu11 -lm -lasound -pthread
DEBUG_CFLAGS=-O0 -g -DDEBUG=1 $(OTHER_CFLAGS)
RELEASE_CFLAGS=-O3 -s $(OTHER_CFLAGS)
smidi: *.[ch]
	$(CC) $(DEBUG_CFLAGS) -o smidi main.c
smidi_release: *.[ch]
	$(CC) $(RELEASE_CFLAGS) -o smidi main.c
install: smidi_release
	mkdir -p /usr/bin
	cp smidi /usr/bin/
