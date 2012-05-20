#flags
CC = clang
CFLAGS = -Wall -g -Wextra

#targets
all: webproxy

webproxy: webproxy.o config.o readline.o utils.o
	$(CC) $(CFLAGS) -lrt -o $@ $^

webproxy.o: webproxy.c
	$(CC) $(CFLAGS) -c $ webproxy.c

tester: tester.o config.o
	$(CC) $(CFLAGS) -o $@ tester.o config.o

tester.o: tester.c
	$(CC) $(CFLAGS) -c $ tester.c

readline.o: readline.c
	$(CC) $(CFLAGS) -c $ readline.c

strutils.o: utils.c
	$(CC) $(CFLAGS) -c $ utils.c

test: test.o config.o
	$(CC) $(CFLAGS) -o test $^

clean:
	rm -f *.o webproxy
