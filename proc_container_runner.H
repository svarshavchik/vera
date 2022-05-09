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

class proc_container_runnerObj {

public:
	proc_container_runnerObj(
		pid_t pid,
		const proc_container &container,
		const std::function<void (const proc_container &, int)> &done);

	pid_t pid;

	std::weak_ptr<const proc_containerObj> container;

	std::function<void (const proc_container &, int)> done;

	void invoke(int wstatus) const;
};

//! Create a running process.

//! The passed-in closure gets called when the process finishes. The
//! closure is responsible for calling find_start_or_stop_to_do().

proc_container_runner create_runner(
	const proc_container &container,
	const std::string &command,
	const std::function<void (const proc_container &, int)> &done
);

#endif
