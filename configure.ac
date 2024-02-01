AC_INIT([httpdirfs],[1.2.5])
AC_CANONICAL_BUILD
AC_CONFIG_FILES([Makefile Doxyfile])
AC_PROG_CC
AC_SEARCH_LIBS([backtrace],[execinfo])

# Because we use $(fuse_LIBS) in $(CFLAGS); see comment in Makefile.in
AX_CHECK_COMPILE_FLAG([-Wunused-command-line-argument],[NUCLA=-Wno-unused-command-line-argument],[-Werror])
AC_SUBST([NUCLA])
AM_INIT_AUTOMAKE([foreign subdir-objects])
PKG_CHECK_MODULES([pkgconf],[gumbo libcurl uuid expat openssl])
# This is separate because we need to be able to use $(fuse_LIBS) in CFLAGS
PKG_CHECK_MODULES([fuse],[fuse])
AC_OUTPUT
