/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "privrequest.H"
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <iostream>

bool request_and_execute_fork(const external_filedesc &efd)
{
	auto [argv, environ_map, pidpipe, releasepipe, outerrpipe] =
		receive_fork(efd);

	if (!pidpipe || !releasepipe || !outerrpipe)
		return false;

	std::vector<char *> argv_ptrs;

	pid_t p;

	while ((p=fork()) == -1)
	{
		std::cerr << "error after fork" << "\n";
		sleep(5);
	}

	if (p > 0)
		// parent - continue with next request.
		return true;

	// child, fork again and exit, so that the child of the
	// child is the one that actually does the work.

	while ((p=fork()) == -1)
	{
		std::cerr << "error after fork" << "\n";
		sleep(5);
	}

	if (p > 0)
		_exit(0);

	// child of child - the one that actually does the work

	p=getpid();

	// SESSION-START: NEW PID

	if (write(pidpipe->fd, &p, sizeof(p)) != sizeof(p))
	{
		std::cerr << "error writing pidpipe\n" << std::flush;
		exit(1);
	}

	for (auto &vec : argv)
		vec.push_back(0);

	argv_ptrs.clear();
	argv_ptrs.reserve(argv.size() + 1);

	for (auto &vec : argv)
		argv_ptrs.push_back(vec.data());
	argv_ptrs.push_back(nullptr);

	for (auto &[name, value] : environ_map)
		setenv(name.c_str(), value.c_str(), 1);

	// SESSION-START: NEW PID REGISTERED
	releasepipe.reset();

	close(0);
	if (open("/dev/null", O_RDONLY) < 0 ||
	    dup2(outerrpipe->fd, STDOUT_FILENO) < 0 ||
	    dup2(outerrpipe->fd, STDERR_FILENO) < 0)
	{
		std::cerr << "error after dup2" << "\n" << std::flush;
		exit(1);
	}

	setsid();
	setpgid(0, 0);

	sigset_t ss;
	sigemptyset(&ss);
	sigprocmask(SIG_SETMASK, &ss, NULL);

	execvp_after_forking(
		argv_ptrs[0],
		argv_ptrs.data()
	);

	fork_status fs;
	fs.failed_errno=errno;
	fs.notregistered=false; // Guaranteed to be registered above

	write(pidpipe->fd, &fs, sizeof(fs));
	_exit(1);
}
