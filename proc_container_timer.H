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

class proc_container_timerObj {

public:
	proc_container_timerObj(
		const proc_container &container,
		const std::function<void (const proc_container &)> &done);

	std::weak_ptr<const proc_containerObj> container;

	std::function<void (const proc_container &)> done;
};

//! Create a new timer.

//! Returns a proc_container_timer handle. Destroying the handle cancels the
//! timer.

proc_container_timer create_timer(
	const proc_container &container,
	time_t timeout,
	const std::function<void (const proc_container &)> &done
);

//! Check and invoke timed out timers.

//! Returns a timeout before run_timers() should be checked again, for
//! a possible timer expiration.

time_t run_timers();

#endif
