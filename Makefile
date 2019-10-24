VERSION=1.2.0

CFLAGS+= -O2 -Wall -Wextra -Wshadow\
	-rdynamic -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE -D_GNU_SOURCE\
	-D_FILE_OFFSET_BITS=64 -DVERSION=\"$(VERSION)\"\
	`pkg-config --cflags-only-I gumbo libcurl fuse uuid expat`
LIBS = -pthread -lgumbo -lcurl -lfuse -lcrypto -luuid -lexpat
COBJS = main.o network.o fuse_local.o link.o cache.o util.o sonic.o

prefix ?= /usr/local

all: httpdirfs

%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -c -o $@ $<

httpdirfs: $(COBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

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

depend: .depend
.depend: src/*.c
	rm -f ./.depend
	$(CC) $(CFLAGS) -MM $^ -MF ./.depend;
include .depend

.PHONY: all doc install clean distclean uninstall depend
