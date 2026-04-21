CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -g

BINARIES := scanner_cli scanner_tests

all: $(BINARIES)

scanner.o: scanner.c scanner.h
	$(CC) $(CFLAGS) -c -o $@ scanner.c

scanner_cli.o: scanner_cli.c scanner.h
	$(CC) $(CFLAGS) -c -o $@ scanner_cli.c

scanner_tests.o: scanner_tests.c scanner.h
	$(CC) $(CFLAGS) -c -o $@ scanner_tests.c

scanner_cli: scanner.o scanner_cli.o
	$(CC) $(CFLAGS) -o $@ scanner.o scanner_cli.o

scanner_tests: scanner.o scanner_tests.o
	$(CC) $(CFLAGS) -o $@ scanner.o scanner_tests.o

test: scanner_tests
	./scanner_tests

clean:
	rm -f *.o $(BINARIES)
