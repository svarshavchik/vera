#include "config.h"
#include "verac.h"
#include "configdirs.h"

#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	int vera_socket;

	if (getpid() == 1)
	{
		const char *chain_to=check_hookfile(HOOKFILE, run_sysinit,
						    "/sbin/init.init",
						    SBINDIR "/vera");

		execv(chain_to, argv);
		perror(chain_to);
		exit(1);
	}

	/*
	** When root poke the private socket, to avoid interference from
	** non-root.
	*/

	vera_socket=
		connect_sun_socket(geteuid() ? PUBCMDSOCKET:PRIVCMDSOCKET);

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
	return 0;
}
