/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/

#include "config.h"
#include "privrequest.H"
#include <vector>
#include <string>
#include <unordered_map>
#include <csignal>
#include <unistd.h>

void privrequest_execve_prep_args(
	std::vector<std::vector<char>> &argv_vec,
	std::vector<char *> &argv_ptrs,
	const std::unordered_map<std::string, std::string> &environ_map
)
{
	for (auto &v:argv_vec)
		v.push_back(0);

	argv_ptrs.reserve(argv_vec.size()+1);
	for (auto &v:argv_vec)
		argv_ptrs.push_back(v.data());

	argv_ptrs.push_back(nullptr);

	for (auto &[var, value] : environ_map)
		setenv(var.c_str(), value.c_str(), 1);

	privrequest_execve_prep_pg();
}

void privrequest_execve_prep_pg()
{
	sigset_t ss;

	sigemptyset(&ss);
	sigprocmask(SIG_SETMASK, &ss, NULL);
	setsid();
	setpgid(0, 0);
}