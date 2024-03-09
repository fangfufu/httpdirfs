bin_PROGRAMS = httpdirfs
httpdirfs_SOURCES = src/main.c src/network.c src/fuse_local.c src/link.c \
	src/cache.c src/util.c src/sonic.c src/log.c src/config.c src/memcache.c
# This has $(fuse_LIBS) in it because there's a bug in the fuse pkgconf:
# it should add -pthread to CFLAGS but doesn't.
# $(NUCLA) is explained in configure.ac.
CFLAGS = -g -O2  -Wall -Wextra -Wshadow $(NUCLA) \
	-rdynamic -D_GNU_SOURCE -DVERSION=\"$(VERSION)\"\
        $(pkgconf_CFLAGS) $(fuse_CFLAGS) $(fuse_LIBS)
LIBS += $(pkgconf_LIBS) $(fuse_LIBS)

SUBDIRS = test

man_MANS = doc/man/httpdirfs.1
CLEANFILES = doc/man/*
DISTCLEANFILES = doc/html/*

# %.o: $(srcdir)/src/%.c
# 	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -c -o $@ $<

# httpdirfs: $(COBJS)
# 	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

man: doc/man/httpdirfs.1

doc/man/httpdirfs.1: httpdirfs
	mkdir -p doc/man
	rm -f doc/man/httpdirfs.1.tmp
	help2man --name "mount HTTP directory as a virtual filesystem" \
		--no-discard-stderr ./httpdirfs > doc/man/httpdirfs.1.tmp
	mv doc/man/httpdirfs.1.tmp doc/man/httpdirfs.1

doc:
	doxygen Doxyfile

format:
	astyle --style=kr --align-pointer=name --max-code-length=80 src/*.c src/*.h

.PHONY: man doc format
