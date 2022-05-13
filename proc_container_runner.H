/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef proc_container_runner_h
#define proc_container_runner_h

#include <unistd.h>
#include <functional>
#include <memory>
#include <string>

#include "proc_containerfwd.H"
#include "proc_container_runnerfwd.H"
#include "current_containers_infofwd.H"

class proc_container_runnerObj {

public:
	proc_container_runnerObj(
		pid_t pid,
		const current_containers_info &all_containers,
		const proc_container &container,
		const std::function<void (
					 const current_containers_callback_info
					 &,
					 int)> &done);

	pid_t pid;

	std::weak_ptr<current_containers_infoObj> all_containers;

	std::weak_ptr<const proc_containerObj> container;

	std::function<void (const current_containers_callback_info &,
			    int)> done;

	void invoke(int wstatus) const;
};

//! Create a running process.

//! The passed-in closure gets called when the process finishes. The
//! closure is responsible for calling find_start_or_stop_to_do().

proc_container_runner create_runner(
	const current_containers_info &all_containers,
	const proc_container &container,
	const std::string &command,
	const std::function<void (const current_containers_callback_info &,
				  int)> &done
);

//! Check if this container has any processes.

bool is_stopped(const proc_container &container);

#endif
