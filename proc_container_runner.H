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

void update_runner_containers(const current_containers &new_current_containers);

#endif
