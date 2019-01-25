VERSION=1.0

CFLAGS+= -g -O2 -Wall -Wextra -D_FILE_OFFSET_BITS=64 -DVERSION=\"$(VERSION)\"
LDFLAGS+= -lgumbo -lcurl -lfuse -lcrypto
COBJS = main.o network.o fuse_local.o link.o

prefix ?= /usr/local

all: httpdirfs

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -c -o $@ $<

httpdirfs: $(COBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^

install:
	install -m 755 -D httpdirfs \
		$(DESTDIR)$(prefix)/bin/httpdirfs
	install -m 644 -D httpdirfs.1 \
		$(DESTDIR)$(prefix)/share/man/man1/httpdirfs.1

doc:
	doxygen Doxyfile

clean:
	rm -rf *.o httpdirfs html

distclean: clean

.PHONY: all install clean distclean
