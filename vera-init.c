#include "config.h"
#include "verac.h"
#include "configdirs.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
	int vera_socket;
	const char *init_version;

	if (getpid() == 1)
	{
		const char *chain_to=check_hookfile(HOOKFILE, run_sysinit,
						    "/sbin/init.init",
						    SBINDIR "/vera");

		execv(chain_to, argv);
		perror(chain_to);
		exit(1);
	}

	// sysdown execs the rc script. init is gone. So, connect_sun_socket()
	// fails and we will loop back here infinitely unless we detect that
	// we came from vera, and go to vlad instead.

	init_version=getenv("INIT_VERSION");

	if (init_version && strncmp(init_version, "vera-", 5) == 0)
	{
		execv(SBINDIR "/vlad", argv);
		perror(SBINDIR "/vlad");
		exit(1);
	}

	// Determine whether vera is hooked, and is running.
	vera_socket=connect_sun_socket(PRIVCMDSOCKET);

	if (vera_socket >= 0)
	{
		close(vera_socket);
		execv(SBINDIR "/vlad", argv);
		perror(SBINDIR "/vlad");
	}
	else
	{
		execv("/sbin/init.init", argv);
		perror("/sbin/init.init");
	}
	exit(1);
	return 0;
}
