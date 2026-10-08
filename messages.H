/*
** Copyright 2022-2026 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef messages_h
#define messages_h

#include <libintl.h>

#define TEXTDOMAIN "vera"

#define _(x) (gettext(x))
#define D_(x) (dgettext(TEXTDOMAIN, x))
#define N_(a,b,n) (ngettext((a),(b),(n)))

#ifdef __cplusplus
extern "C" {
#endif

const char *message_ftsfd();
const char *message_snf();

#ifdef __cplusplus
}
#endif

#endif
