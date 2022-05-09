/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef proc_container_timer_h
#define proc_container_timer_h

#include <unistd.h>
#include <functional>
#include <memory>

#include "proc_containerfwd.H"
#include "proc_container_timerfwd.H"
#include "current_containers_infofwd.H"

class proc_container_timerObj {

public:
	proc_container_timerObj(
		const current_containers_info &all_containers,
		const proc_container &container,
		const std::function<void (
					const current_containers_callback_info &
				)> &done);

	std::weak_ptr<current_containers_infoObj> all_containers;

	std::weak_ptr<const proc_containerObj> container;

	std::function<void (const current_containers_callback_info &)> done;
};

//! Create a new timer.

//! Returns a proc_container_timer handle. Destroying the handle cancels the
//! timer.

proc_container_timer create_timer(
	const current_containers_info &all_containers,
	const proc_container &container,
	time_t timeout,
	const std::function<void (const current_containers_callback_info &
				  )> &done
);

#endif
