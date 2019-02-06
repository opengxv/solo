AC_INIT([solo], [0.1])
AM_INIT_AUTOMAKE([foreign subdir-objects])

AC_CONFIG_HEADERS([config.h])

AC_PROG_CXX
AC_PROG_LEX
AC_PROG_YACC
AC_PROG_LIBTOOL

#export CXXFLAGS="$CXXFLAGS -std=c++11 -O2 -Wall -I. -I.. "
#export CXXFLAGS="$CXXFLAGS -I`pwd`/libs"
#export CXXFLAGS="$CXXFLAGS -I/usr/include/lua5.1"
#export LDFLAGS="$LDFLAGS -L`pwd`/deps/libgx -llua5.1 -lrt -lm -ldl -lz -lmysqlclient"

AC_CONFIG_SUBDIRS([
    libs/libgx
    tools/runproxy
])

AC_CONFIG_FILES([
    Makefile 
    libs/Makefile
    tools/Makefile
])


AC_OUTPUT


