#flags
CC = gcc
CFLAGS = -Wall
LDFLAGS = -s

#targets
all: webproxy

webproxy: webproxy.o config.o
	$(CC) $(CFLAGS) -o $@ $^

webproxy.o: webproxy.c
	$(CC) $(CFLAGS) -c $ webproxy.c

tester: tester.o config.o
	$(CC) $(CFLAGS) -o $@ tester.o config.o

tester.o: tester.c
	$(CC) $(CFLAGS) -c $ tester.c

clean:
	rm -f *.o webproxy
