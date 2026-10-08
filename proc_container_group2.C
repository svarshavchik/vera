/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "proc_container_group.H"

std::string proc_container_group_data::cgroups_dir(std::string name)
{
	return cgroups_dir(get_cgroupfs_base_path(), std::move(name));
}

std::string proc_container_group_data::cgroups_dir(
	std::string_view base,
	std::string name
)
{
	std::string n;

	for (auto &c:name)
		if (c == '/')
			c=':';

	// Should be sufficient for anything else that gets tacked on.
	n.reserve(base.size() + name.size()+30);

	n.insert(n.end(), base.begin(), base.end());

	n.push_back('/');
	n.push_back(':');
	n.insert(n.end(), name.begin(), name.end());

	return n;
}

// Open the cgroup.procs file.

proc_container_group::cgroup_procs_file::cgroup_procs_file(
	const std::string &container_name
): i{cgroups_dir(container_name) + "/cgroup.procs"}
{
	if (i)
		i.imbue(std::locale{"C"});
}

proc_container_group::cgroup_procs_file::~cgroup_procs_file()=default;

std::vector<pid_t> proc_container_group::cgroups_getpids(
	const std::string &name,
	bool child_only
)
{
	std::vector<pid_t> pids;

	std::unordered_set<pid_t> parent_pids;

	cgroup_procs_file cp{name};

	if (cp.i)
	{
		pid_t p;

		while (cp.i >> p)
		{
			pids.push_back(p);

			// Leave parent_pids empty when asking for all pids.

			if (!child_only)
				continue;

			std::ostringstream o;

			o.imbue(std::locale{"C"});

			o << slashprocslash << p << "/status";

			std::ifstream i{o.str()};

			if (!i)
				continue;

			i.imbue(std::locale{"C"});

			std::string l;

			while (std::getline(i, l).good())
			{
				std::istringstream iline{l};

				iline.imbue(std::locale{"C"});

				std::string w;

				iline >> w;

				if (w != "PPid:")
					continue;

				pid_t ppid;

				if (iline >> ppid)
				{
					parent_pids.insert(ppid);
				}
				break;
			}
		}
	}

	// Remove the parent_pids from the pids array.

	auto b=pids.begin(), e=pids.end();

	if (auto p=std::find_if(b, e, [&](auto &p){
		return parent_pids.find(p) == parent_pids.end();
	}); p != e)
	{
		auto q=b;

		while (p != e)
		{
			*q++=*p++;

			p=std::find_if(p, e, [&](auto &p){
				return parent_pids.find(p) == parent_pids.end();
			});
		}

		pids.erase(q, e);
	}
	return pids;
}
