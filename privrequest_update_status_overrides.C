/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "privrequest.H"
#include "proc_loader.H"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>

void update_status_overrides(
	std::unordered_map<std::string, container_state_info> &status,
	const std::string &globaldir,
	const std::string &localdir,
	const std::string &overridedir)
{
	auto overrides=proc_get_overrides(globaldir, localdir, overridedir);

	// Masked containers won't be in the status, let's put them there.

	for (auto &[name, override_status] : overrides)
	{
		if (override_status.get_state() ==
		    proc_override::state_t::masked)
		{
			status[name].state="masked";
		}
	}

	for (auto &[name, info] : status)
	{
		std::filesystem::path path{name};

		while (!path.empty())
		{
			auto iter=overrides.find(path);

			if (iter != overrides.end())
			{
				switch (iter->second.get_state()) {
				case proc_override::state_t::masked:
					break;
				case proc_override::state_t::none:
					break;
				case proc_override::state_t::enabled:
					info.enabled=true;
					break;
				}

				info.resources=iter->second.get_resources();
			}
			path=path.parent_path();
		}
	}
}
