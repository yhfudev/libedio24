#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.60])
AC_INIT(libedio24, 1.0.0, yhfudev@gmail.com)
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE(-Wall subdir-objects)

#magic for conditional check in Makefile:
MK=''; AC_SUBST(MK)
SED=sed

# Checks for programs.
AM_PROG_AR
AC_PROG_CXX
AC_PROG_AWK
AC_PROG_CC
AC_PROG_CPP
AC_PROG_LEX
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_RANLIB

# Checks for libraries.
LT_PREREQ([2.2])
LT_INIT([shared static])
#LT_INIT([disable-static])

dnl Disable doc generation with doxygen option
AC_ARG_ENABLE(doxygen-doc,
              [AC_HELP_STRING([--disable-doxygen-doc],
                              [disable document generation with doxygen])],
              , enable_doxygen_doc="yes")
if test "x$enable_doxygen_doc" = "xyes"; then
  AC_CHECK_PROG(DOXYGEN,doxygen,doxygen,no)
  if test "x$DOXYGEN" = "xno"; then
    enable_doxygen_doc="no"
  fi
fi
dnl where to install the doxygen-generated HTML doc
AC_ARG_WITH(html-docdir,
            [AC_HELP_STRING([--with-html-docdir=DIR],
                            [where to install the doxyten-generated HTML doc [PREFIX/share/doc/libedio24/html]])],
            [htmldocdir="$withval"], [htmldocdir=\$\{prefix\}/share/doc/libedio24/html])
AC_SUBST(htmldocdir)
AM_CONDITIONAL(ENABLE_DOXYGEN_DOC,test "x$enable_doxygen_doc" = "xyes")


# ciut
AC_ARG_WITH(ciut,
            [AS_HELP_STRING([--with-ciut],[Base directory to look for ciut headers and libraries])],
            [CIUT_LIB_DIR="${withval}/lib"
             CIUT_INC_DIR="${withval}/include"],)

AC_ARG_WITH(ciut-lib,
            [AS_HELP_STRING([--with-ciut-lib],[Directory to look for ciut libraries])],
            [CIUT_LIB_DIR="${withval}"],)

AC_ARG_WITH(ciut-include,
            [AS_HELP_STRING([--with-ciut-include],[Directory to look for ciut headers])],
            [CIUT_INC_DIR="${withval}"],)
if test "x$CIUT_INC_DIR" != "x" ; then
   CPPFLAGS="-I${CIUT_INC_DIR} ${CPPFLAGS}"
fi
if test "x$CIUT_LIB_DIR" != "x" ; then
   LDFLAGS="-L${CIUT_LIB_DIR} ${LDFLAGS}"
fi


# libuv
AC_ARG_WITH(libuv,
            [AS_HELP_STRING([--with-libuv],[Base directory to look for libuv headers and libraries])],
            [LIBUV_LIB_DIR="${withval}/lib"
             LIBUV_INC_DIR="${withval}/include"],)

AC_ARG_WITH(libuv-lib,
            [AS_HELP_STRING([--with-libuv-lib],[Directory to look for libuv libraries])],
            [LIBUV_LIB_DIR="${withval}"],)

AC_ARG_WITH(libuv-include,
            [AS_HELP_STRING([--with-libuv-include],[Directory to look for libuv headers])],
            [LIBUV_INC_DIR="${withval}"],)
if test "x$LIBUV_INC_DIR" != "x" ; then
   CPPFLAGS="-I${LIBUV_INC_DIR} ${CPPFLAGS}"
fi
if test "x$LIBUV_LIB_DIR" != "x" ; then
   LDFLAGS="-L${LIBUV_LIB_DIR} ${LDFLAGS}"
fi


# debug
AC_ARG_ENABLE([debug],
	AS_HELP_STRING([--enable-debug],[Compile the debug version (default: disabled)]),
	[enable_debug=$enableval],
	[enable_debug=no])
AM_CONDITIONAL([DEBUG], [test $enable_debug = "yes"])
if test "x$enable_debug" = "xyes"; then
  changequote({,})
  CFLAGS=`echo "$CFLAGS" | $SED -e 's/-O[0-9s]*//g'`
  CXXFLAGS=`echo "$CXXFLAGS" | $SED -e 's/-O[0-9s]*//g'`
  CFLAGS=`echo "$CFLAGS" | $SED -e 's/-g//g'`
  CXXFLAGS=`echo "$CXXFLAGS" | $SED -e 's/-g//g'`
  changequote([,])
  dnl add -O0 only if GCC or ICC is used
  if test "$GCC" = "yes" || test "$ICC" = "yes"; then
    CFLAGS="$CFLAGS -g -O0 -Wall"
    CXXFLAGS="$CXXFLAGS -g -O0 -Wall"
  fi
fi


# Checks for header files.
AC_CHECK_HEADERS([stddef.h stdlib.h string.h sys/time.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_SIZE_T
AC_CHECK_DECLS([sys_siglist])
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_CHECK_FUNCS([gettimeofday localtime_r memset strstr])

AC_CONFIG_FILES([Makefile
                 doc/Makefile
                 doc/Doxyfile
                 src/libedio24.pc
                 src/Makefile
                 utils/Makefile
                ])
AC_OUTPUT
