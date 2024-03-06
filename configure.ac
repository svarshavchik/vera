#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_INIT([vera],[0.90],[mrsam@courier-mta.com])
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
	VALGRIND="valgrind --tool=memcheck --leak-check=yes --show-leak-kinds=all --error-exitcode=1"
fi
AC_SUBST(VALGRIND)

AC_PATH_PROGS(PAGER, less more, none)

if test "$PAGER" = "none"
then
	AC_MSG_ERROR([did not find less or more])
fi

AC_DEFINE_UNQUOTED(PAGER,["${PAGER}"],[Default system pager])

AC_PATH_PROGS(elinks, elinks, none)
AC_PATH_PROGS(links, links, none)

if test "$elinks" != "none"
then
	HTML2TXT="$elinks -dump -no-references"
else
	if test '$links' != "none"
	then
		HTML2TXT="$links -dump /dev/stdin"
	else
		AC_MSG_ERROR(did not find links or elinks)
	fi
fi
AC_SUBST(HTML2TXT)

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

# Configuration

AC_ARG_WITH([installconfdir],
	[AS_HELP_STRING([--with-installconfdir=dir],
		[specify non-default local configuration directory])],
	[INSTALLCONFIGDIR="$withval"],[INSTALLCONFIGDIR="$libdir/vera"])

AC_SUBST(INSTALLCONFIGDIR)

AC_ARG_WITH([localconfdir],
	[AS_HELP_STRING([--with-localconfdir=dir],
		[specify non-default local configuration directory])],
	[CONFIGDIR="$withval"],[CONFIGDIR="$sysconfdir/vera"])

AC_SUBST(CONFIGDIR)

RUNLEVELCONFIG="$CONFIGDIR/runlevels"
AC_SUBST(RUNLEVELCONFIG)

ENVIRONCONFIG="$CONFIGDIR/environ"
AC_SUBST(ENVIRONCONFIG)

LOCALCONFIGDIR="$CONFIGDIR/local"
AC_SUBST(LOCALCONFIGDIR)

OVERRIDECONFIGDIR="$CONFIGDIR/override"
AC_SUBST(OVERRIDECONFIGDIR)

AM_CONDITIONAL(HAVE_DOCS, [test -f ${srcdir}/vera.xml])
AC_CONFIG_FILES([
	Makefile
	po/Makefile.in
	rc.sysvinit.vera
	rc.local.vera
	rc.local_shutdown.vera
])
AC_OUTPUT
