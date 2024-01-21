#ifndef verac_h
#define verac_h

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif
#if 0
}
#endif

/* Make an AF_UNIX connection */

int connect_sun_socket(const char *socketname);

/*
** Parse the inittab file. The callback gets invoked for every line.
**
** Empty/commented lines result in a null identifier.
*/

void parse_inittab(FILE *fp,
		   void (*cb)(const char *original_line,
			      const char *identifier,
			      const char *runlevels,
			      const char *action,
			      const char *command,
			      void *cbarg),
		   void *cbarg);

#if 0
{
#endif
#ifdef __cplusplus
}
#endif

#endif
