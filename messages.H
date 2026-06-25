/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef messages_h
#define messages_h

#include <libintl.h>

#define _(x) (gettext(x))
#define N_(a,b,n) (ngettext((a),(b),(n)))

#endif
