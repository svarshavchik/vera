#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([vera], [0.01], [mrsam@courier-mta.com])
AC_CONFIG_SRCDIR([testcontroller.C])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([foreign dist-xz -Wno-portability])
AM_GNU_GETTEXT([external])

# Checks for programs.
AC_PROG_CXX
AC_PROG_RANLIB

AC_PATH_PROGS(VALGRIND, valgrind)

if test "$VALGRIND" != ""
then
	VALGRIND="valgrind --tool=memcheck --leak-check=yes"
fi
AC_SUBST(VALGRIND)

# Checks for libraries.

# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

AC_CONFIG_FILES([Makefile po/Makefile.in])
AC_OUTPUT
