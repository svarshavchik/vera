/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef proc_container_timer_h
#define proc_container_timer_h

#include <unistd.h>
#include <functional>
#include <memory>
#include <time.h>

#include "proc_containerfwd.H"
#include "proc_container_timerfwd.H"
#include "proc_timer.H"
#include "current_containers_infofwd.H"

class proc_container_timerObj : public proc_timerObj {

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

	std::weak_ptr<current_containers_infoObj> all_containers;

	std::weak_ptr<const proc_containerObj> container;

	/*! Invoke the callback, if the container still exists

	*/
	void triggered() override;

	/*! Update the container

	This is called when all containers are reloaded. New containers are
	allocated, and old ones are deleted. The old ones are not deleted yet,
	so check if container still exists, if so, update the weak_ptr to point
	to the new container. If not, do nothing.
	*/
	void update_timer_containers(const current_containers &) override;

	const std::function<void (const current_containers_callback_info &)> done;
};

/*! Create a new timer that's related to a container.

If the container goes out of scope and gets destroyed, the timer never runs.

Returns a proc_container_timer handle. Destroying the handle cancels the
timer.
*/

proc_container_timer create_timer(
	const current_containers_info &all_containers,
	const proc_container &container,
	time_t timeout,
	const std::function<void (const current_containers_callback_info &
				  )> &done
);

void update_timer_containers(const current_containers &new_current_containers);

#endif
