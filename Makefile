CC=gcc
CFLAGS= -g -O2 -Wall -Wextra -lgumbo -lcurl -lfuse -lcrypto \
	-D_FILE_OFFSET_BITS=64
OBJ = main.o network.o fuse_local.o

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

httpdirfs: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

install:
	cp httpdirfs ${HOME}/bin/

doc:
	doxygen Doxyfile

.PHONY: clean

clean:
	rm -rf *.o httpdirfs html
