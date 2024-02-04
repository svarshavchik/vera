/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef proc_container_timer_h
#define proc_container_timer_h

#include <unistd.h>
#include <functional>
#include <memory>
#include <map>
#include <time.h>

#include "proc_containerfwd.H"
#include "proc_container_timerfwd.H"
#include "current_containers_infofwd.H"

class proc_container_timerObj;

typedef std::multimap<time_t, std::weak_ptr<proc_container_timerObj>
		      > current_timers_t;

class proc_container_timerObj {

public:
	/*! create_timer() constructs this, inserts current_timers, set my_iter
	 */

	proc_container_timerObj(
		const current_containers_info &all_containers,
		const proc_container &container,
		time_t time_start,
		time_t time_end,
		const std::function<void (
					const current_containers_callback_info &
				)> &done);

	//! Destructor removes this from my_iter, if it's not end()
	~proc_container_timerObj();

	current_timers_t::iterator my_iter;

	const time_t time_start;
	const time_t time_end;
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

void update_timer_containers(const current_containers &new_current_containers);

#endif
