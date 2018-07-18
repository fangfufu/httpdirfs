CC=gcc
CFLAGS=-I. -Wall -Wextra -lgumbo -lcurl -g
DEPS =
OBJ = main.o link.o test.o http.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

mount-http-dir: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: clean

clean:
	rm -f *.o mount-http-dir
