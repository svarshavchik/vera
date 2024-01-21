#include "config.h"
#include "verac.h"
#include <sys/un.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>

int connect_sun_socket(const char *socketname)
{
	int fd=socket(PF_UNIX, SOCK_STREAM, 0);
	struct sockaddr_un sun;

	memset(&sun, 0, sizeof(sun));
	sun.sun_family=AF_UNIX;
	strcpy(sun.sun_path, socketname);

	if (connect(fd, (struct sockaddr *)&sun, sizeof(sun)) < 0)
	{
		close(fd);
		fd=-1;
	}

	return fd;
}
