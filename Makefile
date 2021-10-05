VERSION = 1.2.3

CFLAGS += -g -O2  -Wall -Wextra -Wshadow \
	-fsanitize=undefined -fanalyzer -Wno-analyzer-file-leak \
	-rdynamic -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -DVERSION=\"$(VERSION)\"\
	`pkg-config --cflags-only-I gumbo libcurl fuse uuid expat`
LDFLAGS += `pkg-config --libs-only-L gumbo libcurl fuse uuid expat`
LIBS = -pthread -lgumbo -lcurl -lfuse -lcrypto -lexpat
COBJS = main.o network.o fuse_local.o link.o cache.o util.o sonic.o log.o\
	config.o memcache.o

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

all: httpdirfs

%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -c -o $@ $<

httpdirfs: $(COBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

install:
ifeq ($(OS),Linux)
	install -m 755 -D httpdirfs \
		$(DESTDIR)$(prefix)/bin/httpdirfs
	install -m 644 -D doc/man/httpdirfs.1 \
		$(DESTDIR)$(prefix)/share/man/man1/httpdirfs.1
endif
ifeq ($(OS),FreeBSD)
	install -m 755 httpdirfs \
		$(DESTDIR)$(prefix)/bin/httpdirfs
	gzip -f -k doc/man/httpdirfs.1
	install -m 644 doc/man/httpdirfs.1.gz \
		$(DESTDIR)$(prefix)/share/man/man1/httpdirfs.1.gz
endif
ifeq ($(OS),Darwin)
	install -d $(DESTDIR)$(prefix)/bin
	install -m 755 httpdirfs \
		$(DESTDIR)$(prefix)/bin/httpdirfs
	install -d $(DESTDIR)$(prefix)/share/man/man1
	install -m 644 doc/man/httpdirfs.1 \
		$(DESTDIR)$(prefix)/share/man/man1/httpdirfs.1
endif

man: httpdirfs
	help2man --no-discard-stderr ./httpdirfs > doc/man/httpdirfs.1

doc:
	doxygen Doxyfile

format:
	astyle --style=kr --align-pointer=name --max-code-length=80 src/*.c src/*.h

clean:
	-rm -f src/*.h~
	-rm -f src/*.c~
	-rm -f src/*orig
	-rm -f *.o
	-rm -f httpdirfs

distclean: clean
	-rm -rf doc/html
	-rm -rf doc/man/httpdirfs.1

uninstall:
	-rm -f $(DESTDIR)$(prefix)/bin/httpdirfs
ifeq ($(OS),Linux)
	-rm -f $(DESTDIR)$(prefix)/share/man/man1/httpdirfs.1
endif
ifeq ($(OS),FreeBSD)
	-rm -f $(DESTDIR)$(prefix)/man/man1/httpdirfs.1.gz
endif
ifeq ($(OS),Darwin)
	-rm -f $(DESTDIR)$(prefix)/share/man/man1/httpdirfs.1
endif

depend: .depend
.depend: src/*.c
	rm -f ./.depend
	$(CC) $(CFLAGS) -MM $^ -MF ./.depend;
include .depend

.PHONY: all man doc install clean distclean uninstall depend format
