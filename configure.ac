#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_INIT([vera],[0.01],[mrsam@courier-mta.com])
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
	VALGRIND="valgrind --tool=memcheck --leak-check=yes --error-exitcode=1"
fi
AC_SUBST(VALGRIND)

# Checks for libraries.

save_CPPFLAGS="$CPPFLAGS"
save_LIBS="$LIBS"

if test "$YAMLLIBS" = ""
then
	YAMLLIBS="-lyaml"
fi

CPPFLAGS="$YAMLCPPFLAGS $CPPFLAGS"
LIBS="$YAMLLIBS $LIBS"

AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <yaml.h>
]], [[
   yaml_get_version(0, 0, 0);
]])],[ : ],[AC_MSG_ERROR(Test link with LibYAML failed. Please install the LibYAML library and its development package. See config.log for more details about this error.)])
LIBS="$save_LIBS"
CPPFLAGS="$save_CPPFLAGS"

AC_SUBST(YAMLCPPFLAGS)
AC_SUBST(YAMLLIBS)


# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

AC_CONFIG_FILES([Makefile po/Makefile.in])
AC_OUTPUT
