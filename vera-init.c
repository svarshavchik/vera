#include "config.h"
#include "verac.h"
#include "configdirs.h"

#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

/*
  Chain to the real init, or vlad, depending upon our luck with the probe
  socket.
*/

static void chain(int socket_probe, char **argv)
{
	if (socket_probe < 0)
	{
		execv("/sbin/init.init", argv);
		perror("/sbin/init.init");
	}
	else
	{
		execv(SBINDIR "/vlad", argv);
		perror(SBINDIR "/vlad");
	}
	exit (1);
}

int main(int argc, char **argv)
{
	int vera_socket;

	/*
	** When root poke the private socket, to avoid interference from
	** non-root.
	*/

	vera_socket=
		connect_sun_socket(geteuid() ? PUBCMDSOCKET:PRIVCMDSOCKET);

	if (vera_socket >= 0)
		close(vera_socket);

	chain(vera_socket, argv);
	return 0;
}
