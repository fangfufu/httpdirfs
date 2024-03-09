AC_INIT([httpdirfs],[1.2.5])
AC_CANONICAL_BUILD
AC_PROG_CC
AC_PROG_CXX
AC_SEARCH_LIBS([backtrace],[execinfo])

# Because we use $(fuse_LIBS) in $(CFLAGS); see comment in Makefile.in
AX_CHECK_COMPILE_FLAG([-Wunused-command-line-argument],[NUCLA=-Wno-unused-command-line-argument],[-Werror])
AC_SUBST([NUCLA])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_FILES([Makefile Doxyfile])
# This is separate because we need to be able to use $(fuse_LIBS) in CFLAGS
PKG_CHECK_MODULES([fuse],[fuse])

AC_ARG_ENABLE([unittests],
    [AS_HELP_STRING([--enable-unittests],
    [build the unit tests (default is NO)])],
    [unittests=$enableval], [unittests=no])
AM_CONDITIONAL([UNITTESTS], [test "x$unittests" = "xyes"])
if test "x$unittests" = "xyes"; then
    AC_CONFIG_FILES([test/Makefile])
    PKG_CHECK_MODULES([pkgconf],[gumbo libcurl uuid expat openssl gtest])
else
    PKG_CHECK_MODULES([pkgconf],[gumbo libcurl uuid expat openssl])
fi

AC_OUTPUT
