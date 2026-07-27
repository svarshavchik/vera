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

void sort_pids(std::unordered_map<pid_t, pid_info> &processes,
	       hier_pid_info::hier_pids &pids)
{
	std::unordered_map<pid_t, hier_pid_info *> parent_pid_lookup;

	// Take each pid in the original map

	while (!processes.empty())
	{
		auto b=processes.begin();

		// See if its parent also exists in the map, if so look at
		// the parent, and proceed recursively.

		auto p=processes.find(b->second.ppid);

		size_t failsafe=processes.size();

		while (p != processes.end() && --failsafe)
		{
			b=p;
			p=processes.find(b->second.ppid);
		}

		// We maintain a lookup map of pids to their entry in the
		// hier_pids list.

		auto parent=parent_pid_lookup.find(b->second.ppid);

		if (parent != parent_pid_lookup.end())
		{
			// This pid is a child of a parent pid that's already
			// in the pids, so move it there, and update the
			// parent_pid_lookup so that the moved pid can be found
			// in the hier_pids

			parent_pid_lookup[b->first]=&(
				parent->second->child_pids[b->first]=
					hier_pid_info{
						std::move(b->second)
					}
			);
		}
		else
		{
			// Move the pid to the hier pid and update the
			// lookup map.
			parent_pid_lookup[b->first]=&(
				pids[b->first]=hier_pid_info{
					std::move(b->second),
					{}
				}
			);
		}
		processes.erase(b);
	}
}
