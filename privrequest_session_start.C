/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "configdirs.h"
#include "connect_vera_priv.H"
#include "create_default_vera_socket.H"
#include "export_fd_to_env.H"
#include "privrequest.H"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>

extern void exec_vera(char **);

bool request_session_start(const external_filedesc &efd)
{
	return efd->write_all(std::string{"session-start\n"});
}

std::string session_start(std::string session)
{
	// A pipe between the current process and a child process that gets
	// forked off.

	auto opt_sp = create_vera_socketpair();
	if (!opt_sp)
		return strerror(errno);
	auto &[cmdfd1, cmdfd2] = *opt_sp;

	pid_t p=fork();

	if (p < 0)
	{
		return strerror(errno);
	}

	if (p)
	{
		// The parent closes the child end of the pipe, and reads the
		// result of the session start request.

		cmdfd2 = {};

		return cmdfd1->readln();
	}

	// The child process closes the parent side of the pipe, and sends
	// a session start request to vera.
	cmdfd1 = {};

	// Create a socket that vera will have to the session manager instance
	// of vera.
	auto opt_sp2 = create_vera_socketpair();
	if (!opt_sp2)
	{
		(void)cmdfd2->write_all(
			_("socketpair failed\n")
		);
		exit(1);
	}
	auto &[s1, s2] = *opt_sp2;

	// Prepare the session start request, and include the vera side of the
	// socket.

	auto cmdfd=connect_vera_priv();
	std::vector<external_filedesc> cmdfds{tmpfile_filedesc()};

	if (!request_session_start(cmdfds[0]))
	{
		(void)cmdfd2->write_all(
			_("Failed to request session start\n")
		);
		exit(0);
	}
	cmdfds.push_back(std::move(s2));
	s2={};

	wrap_request_send_fd(cmdfd, cmdfds);

	cmdfds.clear();

	// Read the result of the request from vera pid 1. If it's an error
	// then forward it to the parent process.

	auto ret=get_start_status(cmdfd);

	if (ret != "")
	{
		(void)cmdfd2->write_all(ret + "\n");
		exit(0);
	}

	// pid 1 gave us the blessing. Save the pipe to the original parent
	// process (CMDFD), and the connection file descriptor (LISTENFD)
	// as environment variables.

	if (!export_fd_to_env(cmdfd2, VERA_CMDFD_ENV_VAR))
	{
		(void)cmdfd2->write_all(
			_("failed to prepare environment variable\n")
		);
		exit(0);
	}

	if (!export_fd_to_env(s1, VERA_SOCKET_ENV_VAR))
	{
		(void)cmdfd2->write_all(
			_("failed to prepare environment variable\n")
		);
		exit(0);
	}

	s1={};
	cmdfd2={};

	static char vera[]="vera";

	std::vector<char *> argv{&vera[0], session.data(), nullptr};

	exec_vera(argv.data());

	return strerror(errno);
}
