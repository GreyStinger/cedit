CC=clang
CARGS=-Wall -Wextra -pedantic

all:
	$(CC) cedit.c -o cedit $(CARGS) -std=c99
	# $(CC) cedit.c -o cedit17 $(CARGS) -std=c17

17:
	$(CC) cedit.c -o cedit17 $(CARGS) -std=c17

clean:
	rm -f ./cedit ./cedit17