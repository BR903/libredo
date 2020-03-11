dnl * configure.ac: the input file to autoconf.
dnl *

AC_INIT([libredo], [0.8], [breadbox@muppetlabs.com], [libredo],
        [http://www.muppetlabs.com/~breadbox/software/libredo.html])
AC_CONFIG_SRCDIR([redo.c])


dnl * Verify the toolchain.

test -z "$CFLAGS" && CFLAGS="-Wall -Wextra -O3"
test -z "$ARFLAGS" && ARFLAGS="crs"

AC_PROG_CC
AC_PROG_INSTALL
AC_CHECK_TOOL([AR], [ar], [:])
if test "$AR" = ":" ; then
  AC_MSG_ERROR([Cannot find the library archiver (ar).
    Please set AR to point to the archiver program, and ARFLAGS to the options
    used to create a new archive.])
fi 
AC_SUBST([ARFLAGS])


dnl * Checks for the standard C library features that libredo uses.

AC_CHECK_HEADERS([stdlib.h stdint.h string.h assert.h], [],
    [AC_MSG_ERROR([Standard C header files not found.])])
AC_CHECK_FUNCS([memcpy memcmp memset malloc free], [],
    [AC_MSG_ERROR([Standard C library functions not found.])])
AC_CHECK_TYPES([size_t, uint32_t], [],
    [AC_MSG_ERROR([Standard C types not defined.])])


dnl * The ncurses library is only used by the sample program. If it is
dnl * absent, print a warning and drop the sample program from "make all".

ncurses_avail=0
AC_CHECK_LIB([ncurses], [initscr],
             [AC_CHECK_HEADERS([ncurses.h], [ncurses_avail=1])])
if test $ncurses_avail = 1 ; then
  AC_SUBST([NCURSES_AVAIL], ["NCURSES_AVAIL = 1"])
else
  AC_SUBST([NCURSES_AVAIL], ["#NCURSES_AVAIL = 1"])
  AC_MSG_WARN([The ncurses library header files are not installed.
    ncurses is only used for the sample program, so feel free to ignore
    this message if you only need to build the actual library.
    If you are on a debian-based system, you can fix this issue by running
    the command: "sudo apt install libncurses5-dev".])
fi


dnl * Produce our makefile.

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
