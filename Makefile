CC := gcc
CFLAGS := -Wall
PROGS := client clientout server

all: $(PROGS)

client: client.c
	$(CC) $(CFLAGS) $^ -o $@ -pthread

clientout: clientout.c
	$(CC) $(CFLAGS) $^ -o $@

server: server.c
	$(CC) $(CFLAGS) $^ -o $@ -pthread

setup: all
	mkdir -p utente1
	mkdir -p utente2
	cp client clientout utente1
	cp client clientout utente2

clean:
	rm -f $(PROGS)

distclean: clean
	rm -rf log.txt utente1 utente2

.PHONY: all setup


