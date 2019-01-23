CC=gcc
CFLAGS+= -g -O2 -Wall -Wextra -D_FILE_OFFSET_BITS=64
LDFLAGS+= -lgumbo -lcurl -lfuse -lcrypto
OBJ = main.o network.o fuse_local.o link.o

PREFIX ?= /usr/local
INSTALL = /usr/bin/install
INSTALL_OPTS = -m 0755

%.o: %.c
	$(CC) -c -o $@ $< $(CPPFLAGS) $(CFLAGS) $(LDFLAGS)

httpdirfs: $(OBJ)
	$(CC) -o $@ $^ $(CPPFLAGS) $(CFLAGS) $(LDFLAGS)

install:
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) $(INSTALL_OPTS) httpdirfs $(DESTDIR)$(PREFIX)/bin

doc:
	doxygen Doxyfile

.PHONY: clean

clean:
	rm -rf *.o httpdirfs html
