VERSION=1.0

CFLAGS+= -g -O2 -Wall -Wextra -D_FILE_OFFSET_BITS=64 -DVERSION=\"$(VERSION)\"
LDFLAGS+= -lgumbo -lcurl -lfuse -lcrypto
OBJ = main.o network.o fuse_local.o link.o

prefix ?= /usr/local

%.o: %.c
	$(CC) -c -o $@ $< $(CPPFLAGS) $(CFLAGS) $(LDFLAGS)

httpdirfs: $(OBJ)
	$(CC) -o $@ $^ $(CPPFLAGS) $(CFLAGS) $(LDFLAGS)

install:
	install -m 755 -D httpdirfs $(DESTDIR)$(prefix)/bin

doc:
	doxygen Doxyfile

.PHONY: clean

clean:
	rm -rf *.o httpdirfs html
