CC=gcc
CFLAGS= -O2 -Wall -Wextra -lgumbo -lcurl -lfuse -D_FILE_OFFSET_BITS=64 \
-DHTTPDIRFS_INFO
OBJ = main.o network.o

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

httpdirfs: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: clean

clean:
	rm -f *.o httpdirfs
