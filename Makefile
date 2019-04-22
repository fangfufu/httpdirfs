VERSION=1.0.5

CFLAGS+= -g -O2 -Wall -Wextra -D_FILE_OFFSET_BITS=64 -DVERSION=\"$(VERSION)\" `pkg-config --cflags-only-I gumbo libcurl fuse`
LDFLAGS+= -lgumbo -lcurl -lfuse -lcrypto `pkg-config --libs-only-L gumbo libcurl fuse`
COBJS = main.o network.o fuse_local.o link.o cache.o util.o

prefix ?= /usr/local

all: httpdirfs

%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -c -o $@ $<

httpdirfs: $(COBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^

install:
	install -m 755 -D httpdirfs \
		$(DESTDIR)$(prefix)/bin/httpdirfs
	install -m 644 -D doc/man/httpdirfs.1 \
		$(DESTDIR)$(prefix)/share/man/man1/httpdirfs.1

doc:
	doxygen Doxyfile

clean:
	-rm -f *.o
	-rm -f httpdirfs
	-rm -rf doc/html

distclean: clean

uninstall:
	-rm -f $(DESTDIR)$(prefix)/bin/httpdirfs
	-rm -f $(DESTDIR)$(prefix)/share/man/man1/httpdirfs.1

.PHONY: all doc install clean distclean uninstall
