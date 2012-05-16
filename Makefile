#flags
CC = clang
CFLAGS = -Wall -O2 -g

#targets
all: webproxy

webproxy: webproxy.o config.o readline.o
	$(CC) $(CFLAGS) -o $@ $^

webproxy.o: webproxy.c
	$(CC) $(CFLAGS) -c $ webproxy.c

tester: tester.o config.o
	$(CC) $(CFLAGS) -o $@ tester.o config.o

tester.o: tester.c
	$(CC) $(CFLAGS) -c $ tester.c

readline.o: readline.c
	$(CC) $(CFLAGS) -c $ readline.c

clean:
	rm -f *.o webproxy
