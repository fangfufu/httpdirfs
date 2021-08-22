VERSION = 1.2.2

CFLAGS += -O2 -Wall -Wextra -Wshadow -rdynamic -D_GNU_SOURCE\
	-D_FILE_OFFSET_BITS=64 -DVERSION=\"$(VERSION)\"\
	`pkg-config --cflags-only-I gumbo libcurl fuse uuid expat`
LDFLAGS += `pkg-config --libs-only-L gumbo libcurl fuse uuid expat`
LIBS = -pthread -lgumbo -lcurl -lfuse -lcrypto -lexpat
COBJS = main.o network.o fuse_local.o link.o cache.o util.o sonic.o log.o\
	config.o

OS := $(shell uname)
ifeq ($(OS),Darwin)
  BREW_PREFIX := $(shell brew --prefix)
  CFLAGS  +=  -I$(BREW_PREFIX)/opt/openssl/include \
	-I$(BREW_PREFIX)/opt/curl/include
  LDFLAGS +=  -L$(BREW_PREFIX)/opt/openssl/lib \
	-L$(BREW_PREFIX)/opt/curl/lib
else
  LIBS    +=  -luuid
endif
ifeq ($(OS),FreeBSD)
  LIBS    +=  -lexecinfo
endif

prefix ?= /usr/local

all: httpdirfs sonicfs

%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -c -o $@ $<

httpdirfs: httpdirfs.o $(COBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

sonicfs: sonicfs.o $(COBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

install: all
ifeq ($(OS),Linux)
	install -m 755 -D httpdirfs \
		$(DESTDIR)$(prefix)/bin/httpdirfs
	install -m 644 -D doc/man/httpdirfs.1 \
		$(DESTDIR)$(prefix)/share/man/man1/httpdirfs.1
	install -m 755 -D sonicfs \
		$(DESTDIR)$(prefix)/bin/sonicfs
	install -m 644 -D doc/man/sonicfs.1 \
		$(DESTDIR)$(prefix)/share/man/man1/sonicfs.1
endif
ifeq ($(OS),FreeBSD)
	install -m 755 httpdirfs\
		$(DESTDIR)$(prefix)/bin/httpdirfs
	gzip -f -k doc/man/httpdirfs.1
	install -m 644 doc/man/httpdirfs.1.gz \
		$(DESTDIR)$(prefix)/man/man1/httpdirfs.1.gz
	install -m 755 sonicfs\
		$(DESTDIR)$(prefix)/bin/sonicfs
	gzip -f -k doc/man/sonicfs.1
	install -m 644 doc/man/sonicfs.1.gz \
		$(DESTDIR)$(prefix)/man/man1/sonicfs.1.gz
endif
ifeq ($(OS),Darwin)
	install -d $(DESTDIR)$(prefix)/bin
	install -m 755 httpdirfs\
		$(DESTDIR)$(prefix)/bin/httpdirfs
	install -m 755 sonicfs\
		$(DESTDIR)$(prefix)/bin/sonicfs
	install -d $(DESTDIR)$(prefix)/share/man/man1
	install -m 644 doc/man/httpdirfs.1 \
		$(DESTDIR)$(prefix)/share/man/man1/httpdirfs.1
	install -m 644 doc/man/sonicfs.1 \
		$(DESTDIR)$(prefix)/share/man/man1/sonicfs.1
endif

doc:
	doxygen Doxyfile

man: all
	help2man --no-discard-stderr ./httpdirfs > doc/man/httpdirfs.1
	help2man --no-discard-stderr ./sonicfs > doc/man/sonicfs.1

clean:
	-rm -f *.o
	-rm -f httpdirfs sonicfs

distclean: clean
	-rm -rf doc/html

uninstall:
	-rm -f $(DESTDIR)$(prefix)/bin/httpdirfs
	-rm -f $(DESTDIR)$(prefix)/bin/sonicfs
ifeq ($(OS),Linux)
	-rm -f $(DESTDIR)$(prefix)/share/man/man1/httpdirfs.1
	-rm -f $(DESTDIR)$(prefix)/share/man/man1/sonicfs.1
endif
ifeq ($(OS),FreeBSD)
	-rm -f $(DESTDIR)$(prefix)/man/man1/httpdirfs.1.gz
	-rm -f $(DESTDIR)$(prefix)/man/man1/sonicfs.1.gz
endif
ifeq ($(OS),Darwin)
	-rm -f $(DESTDIR)$(prefix)/share/man/man1/httpdirfs.1
	-rm -f $(DESTDIR)$(prefix)/share/man/man1/sonicfs.1
endif

depend: .depend
.depend: src/*.c
	rm -f ./.depend
	$(CC) $(CFLAGS) -MM $^ -MF ./.depend;
include .depend

.PHONY: all doc man install clean distclean uninstall depend
