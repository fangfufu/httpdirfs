CC=gcc
CFLAGS= -g -Wall -Wextra -lgumbo -lcurl -lfuse -D_FILE_OFFSET_BITS=64 \
-DHTTPDIRFS_INFO -DHTTPDIRFS_SINGLE
OBJ = main.o network.o

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

httpdirfs: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: clean

clean:
	rm -f *.o httpdirfs
