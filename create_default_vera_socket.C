/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/

#include "config.h"
#include "create_default_vera_socket.H"
#include "external_filedesc.H"
#include "hook.H"
#include "log.H"
#include "messages.H"
#include "verac.h"
#include <charconv>
#include <errno.h>
#include <fcntl.h>
#include <string>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

external_filedesc create_default_vera_socket()
{
	umask(0111);
	std::string privcmdsocket=PRIVCMDSOCKET;
	std::string privcmdsocket_tmp=privcmdsocket + ".tmp";
	int fd;

	// Create the privileged command socket. This is the pid 1 server,
        // a failure is critical, so it's a socket, or bust...

	while ((fd=try_create_vera_socket(privcmdsocket_tmp,
                privcmdsocket)) < 0)
	{
		log_message(privcmdsocket +
			    _(": socket creation failure: ") +
			    strerror(errno));
		sleep(5);
	}

        umask(022);
	return std::make_shared<external_filedescObj>(fd);
}

external_filedesc import_default_vera_socket()
{
        int save_listenfd;

        const char *listenfd=getenv(VERA_SOCKET_ENV_VAR);
        if (!listenfd || std::from_chars(listenfd, listenfd+strlen(listenfd),
                                save_listenfd).ec !=
                        std::errc{})
        {
                throw "failed to parse saved session descriptors";
        }
        auto socket_fd=std::make_shared<external_filedescObj>(
                save_listenfd
        );

        fcntl(socket_fd->fd, F_SETFD, FD_CLOEXEC);
        return socket_fd;
}

external_filedesc import_vera_cmdfd()
{
        int save_cmdfd;

        const char *cmdfd=getenv(VERA_CMDFD_ENV_VAR);
        if (!cmdfd || std::from_chars(cmdfd, cmdfd+strlen(cmdfd),
                                save_cmdfd).ec !=
                        std::errc{})
        {
                throw "failed to parse saved session descriptors";
        }
        auto socket_fd=std::make_shared<external_filedescObj>(
                save_cmdfd
        );

        fcntl(socket_fd->fd, F_SETFD, FD_CLOEXEC);

        return socket_fd;
}

void unset_vera_fd_env_vars()
{
        unsetenv(VERA_SOCKET_ENV_VAR);
        unsetenv(VERA_CMDFD_ENV_VAR);
}
