AM_CPPFLAGS = -I../src $(gtest_CPPFLAGS)
AM_CXXFLAGS = -std=c++14
LDADD = $(fuse_LIBS) $(pkgconf_LIBS) -lgtest_main $(gtest_LIBS)

check_PROGRAMS = \
	test_utilities

TESTS = $(check_PROGRAMS)

noinst_PROGRAMS = test_utilities
test_utilities_SOURCES = \
	test_utilities.cpp \
	../src/util.c \
	../src/log.c \
	../src/config.c

