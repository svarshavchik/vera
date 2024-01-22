#ifndef verac_h
#define verac_h

#include <stdio.h>

#define PRIVCMDSOCKET LOCALSTATEDIR "/vera.priv"
#define PUBCMDSOCKET LOCALSTATEDIR "/vera.pub"

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

/*
** Parse the inittab file and run all sysinit entries.
**
** This is executed, in C, by vera-init, after which the system should be
** sufficiently up to cut over to vera.
*/
void run_sysinit(const char *etc_inittab);

#if 0
{
#endif
#ifdef __cplusplus
}
#endif

#endif
