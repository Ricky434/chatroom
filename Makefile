CC := gcc
CFLAGS := -Wall
PROGS := client clientout server3

client: client.c
	$(CC) $(CFLAGS) $^ -o $@ -pthread

clientout: clientout.c
	$(CC) $(CFLAGS) $^ -o $@

server3: server3.c
	$(CC) $(CFLAGS) $^ -o $@ -pthread


.PHONY: all

all: $(PROGS)
