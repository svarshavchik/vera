/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "privrequest.H"
#include <optional>
#include <tuple>
#include <utility>

std::optional<
	std::tuple<std::vector<external_filedesc>, external_filedesc>
>
setup_cmd(bool nowait)
{
	std::vector<external_filedesc> cmdfds{tmpfile_filedesc()};
	external_filedesc stdoutcc;
	if (!nowait)
	{
		auto opt_sp = create_vera_socketpair();
		if (!opt_sp)
			return std::nullopt;
		auto &[socket1, socket2] = *opt_sp;
		stdoutcc = std::move(socket1);
		cmdfds.push_back(std::move(socket2));
	}
	return std::make_tuple(std::move(cmdfds), std::move(stdoutcc));
}
