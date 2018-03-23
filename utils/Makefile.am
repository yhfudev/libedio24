
EXTRA_DIST=

AM_LDFLAGS=
AM_CFLAGS=

DEFS += \
    `getconf LFS_CFLAGS` \
    `getconf LFS64_CFLAGS` \
    -D_GNU_SOURCE \
    -D_FILE_OFFSET_BITS=64 \
    -DHAVE_MMAP64=1 \
    -DUSE_EDIO24_SERVER=1 \
    $(NULL)

AM_CFLAGS += \
    -I$(top_srcdir)/src \
    -I$(top_srcdir)/include/ \
    -I$(top_builddir)/ \
    -I$(top_builddir)/src/ \
    -I$(top_builddir)/include/ \
    $(NULL)

AM_LDFLAGS += \
    -L$(top_builddir)/src/ \
    `getconf LFS_LDFLAGS` \
    `getconf LFS64_LDFLAGS` \
    $(NULL)

#AM_LDFLAGS += -L$(top_builddir)/src/ -ledio24

#AM_CFLAGS += `pkg-config --cflags libuv`
#AM_LDFLAGS += `pkg-config --libs libuv`
AM_CFLAGS += $(LIBUV_CFLAGS)
AM_LDFLAGS+= $(LIBUV_LDFLAGS) $(LIBUV_LIBS)

#AM_LDFLAGS += -pthread

if DEBUG
# use "valgrind --tool=memcheck --leak-check=yes" to check memory leak, MemWatch will drag the program.
#DEFS+=-DMEMWATCH
DEFS+= -DDEBUG=1
AM_CFLAGS +=-g -Wall
AM_LDFLAGS += -lbfd

else
AM_CFLAGS+=-O3 -Wall
endif

# add "VALGRIND = valgrind --gen-suppressions=all" in your makefile to get the content of .supp
# default valgrind supp file: /usr/lib/valgrind/default.supp
@VALGRIND_CHECK_RULES@
VALGRIND_SUPPRESSIONS_FILES = libedio24-valgrind-test.supp
EXTRA_DIST += libedio24-valgrind-test.supp

#if USE_VALGRIND
#TESTS_ENVIRONMENT=$(VALGRIND) --quiet --trace-children=yes --error-exitcode=1 --dsymutil=yes --leak-check=yes 
#endif

#check-valgrind: check $(TESTS)
#	@which valgrind >/dev/null || (echo "Running the valgrind target without having valgrind. Perhaps install it first?"; exit 1;)
#	@for test in $$(echo $(TESTS) | sed 's/uitls\//utils\/.libs\//g') ; do \
#	valgrind --dsymutil=yes --suppressions=$(srcdir)/valgrind.suppressions --gen-suppressions=all --error-exitcode=1 ./$${test} ; \
#        done
#        CK_FORK=no LD_LIBRARY_PATH=$(abs_top_builddir)/.libs valgrind --error-exitcode=1 --leak-resolution=low --quiet \
#            --leak-check=full --show-possibly-lost=no --suppressions=tests/valgrind.suppressions $${test} ;


#AM_CFLAGS+=-fpermissive


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

#AM_CFLAGS+=$(EXT_FLAGS)


# force the autoconf use g++ to link the c program with c++ lib
dummy.cpp:
	touch $@
clean-local-dummy:
	-rm -rf dummy.cpp

ciutexc.c:
	-echo "#define CIUT_PLACE_MAIN 1" > $@
	-echo "#include <ciut.h>" >> $@
	-echo "#include \"../src/libedio24.c\"" >> $@
	-echo "int main(int argc, char * argv[]) { return ciut_main(argc, argv); }" >> $@
clean-local-check:
	-rm -rf ciutexc.c

clean-local: clean-local-check clean-local-dummy


#noinst_PROGRAMS=ciutexc
TESTS=ciutexc
check_PROGRAMS=ciutexc
bin_PROGRAMS=edio24cli edio24sim gencctcmd


ciutexc_LDADD = -luv
ciutexc_CFLAGS = -DCIUT_ENABLED=1 $(AM_CFLAGS)
ciutexc_LDFLAGS = $(AM_LDFLAGS)


gencctcmd_SOURCES= \
    gencctcmd.c \
    utils.c \
    $(NULL)

ciutexc_SOURCES= \
    gencctcmd.c \
    utils.c \
    ciutexc.c \
    $(NULL)

edio24cli_SOURCES= \
    edio24cli.c \
    utils.c \
    $(NULL)

edio24sim_SOURCES= \
    edio24sim.c \
    $(NULL)

EXTRA_DIST += \
    utils.h \
    $(NULL)

edio24cli_LDADD = $(top_builddir)/src/libedio24.la -luv -ldl
edio24cli_CPPFLAGS = $(AM_CFLAGS)
#edio24cli_LDFLAGS = -L$(top_builddir)/src/ -ledio24 $(AM_LDFLAGS)
edio24cli_LDFLAGS = $(AM_LDFLAGS)

edio24sim_LDADD = $(top_builddir)/src/libedio24.la -luv -ldl
edio24sim_CPPFLAGS = $(AM_CFLAGS)
#edio24sim_LDFLAGS = -L$(top_builddir)/src/ -ledio24 $(AM_LDFLAGS)
edio24sim_LDFLAGS = $(AM_LDFLAGS)


