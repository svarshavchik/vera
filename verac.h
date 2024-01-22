#ifndef verac_h
#define verac_h

#include <stdio.h>

#define PRIVCMDSOCKET LOCALSTATEDIR "/vera.priv"
#define PUBCMDSOCKET LOCALSTATEDIR "/vera.pub"
#define HOOKFILE CONFIGDIR "/hook"

#define HOOKED_ON	"hooked=on"
#define HOOKED_ONCE	"hooked=once"

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

/*
** Check and process the hook file that's read at boot by pid 1, to determine
** what to do.
**
** hookfile is the HOOKFILE installed by "vlad hook".
**
** run_sysinit_cb points to run_sysinit which gets invoked if vera is to start.
**
** Returns either the init_path, if the hook file is not installed, or the
** vera_path.
*/

const char *check_hookfile(const char *hookfile,
			   void (*run_sysinit_cb)(const char *inittab),
			   const char *init_path,
			   const char *vera_path);

#if 0
{
#endif
#ifdef __cplusplus
}
#endif

#endif
