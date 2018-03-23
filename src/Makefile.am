
AM_LDFLAGS=
AM_CFLAGS=
EXTRA_DIST=

#DEFS+=-std=c99

if DEBUG
# use "valgrind --tool=memcheck --leak-check=yes" to check memory leak, MemWatch will drag the program.
#DEFS+=-DMEMWATCH=1
DEFS+=-DDEBUG=1
AM_CFLAGS += -g -O0 -Wall

else
AM_CFLAGS += -O3 -Wall
endif

DEFS += \
    `getconf LFS_CFLAGS` \
    `getconf LFS64_CFLAGS` \
    -D_GNU_SOURCE \
    -D_FILE_OFFSET_BITS=64 \
    -DUSE_EDIO24_SERVER=1 \
    $(NULL)


EXT_FLAGS=
@MK@GITNUMTMP=$(shell cd "$(top_srcdir)"; A=$$(git show | head -n 1 | awk '{print $$2}'); echo $${A:0:7}; cd - > /dev/null )
#@MK@SVNNUMTMP=$(shell cd "$(top_srcdir)"; LC_ALL=C svn info | grep -i Revision | awk '{print $$2}'; cd - > /dev/null )
#@MK@ifeq ($(SVNNUMTMP),)
#EXT_FLAGS+= -DSVN_VERSION='"${GITNUMTMP}"'
#@MK@else
#EXT_FLAGS+= -DSVN_VERSION='"${SVNNUMTMP}"'
#@MK@endif
@MK@ifeq ($(GITNUMTMP),)
@MK@else
EXT_FLAGS+= -DSVN_VERSION='"${GITNUMTMP}"'
@MK@endif
DEFS+=$(EXT_FLAGS)


# public headers
# it will be moved to include/Makefile.am
include_HEADERS = \
    $(top_srcdir)/include/libedio24.h \
    $(NULL)

EXTRA_DIST += \
    libedio24.pc.in \
    $(NULL)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libedio24.pc


lib_LTLIBRARIES=libedio24.la
libedio24_la_SOURCES= \
    libedio24.c \
    $(NULL)

libedio24_la_CFLAGS= $(AM_CFLAGS)\
    -I $(top_srcdir)/include/ \
    -I $(top_builddir)/include/ \
    $(NULL)


