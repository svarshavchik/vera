/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/

#include "config.h"
#include "export_fd_to_env.H"
#include <charconv>
#include <limits>
#include <unistd.h>

bool export_fd_to_env(const external_filedesc &fd, const char *varname)
{
	int fd_dup = dup(fd->fd);
	if (fd_dup < 0)
		return false;

	char env_buf[std::numeric_limits<int>::digits10 + 1];

	auto [ptr, ec] =
		std::to_chars(env_buf, env_buf + sizeof(env_buf) - 1, fd_dup);
	if (ec != std::errc{})
		return false;

	*ptr = 0;
	if (setenv(varname, env_buf, 1) != 0)
		return false;

	return true;
}
