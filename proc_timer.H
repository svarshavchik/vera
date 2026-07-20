/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef proc_timer_h
#define proc_timer_h

#include <unistd.h>
#include <memory>
#include <map>
#include <time.h>

#include "proc_timerfwd.H"
#include "current_containers_infofwd.H"
class proc_timerObj;

typedef std::multimap<time_t, std::weak_ptr<proc_timerObj>
		      > current_timers_t;

class proc_timerObj {

public:
	/*! Construct a new timer

	The constructor sets my_iter to current_timers.end(). install_timer()
	inserts it into current_timers at time_start, if all goes well, and
	updates my_iter accordingly. When the timer is triggered my_iter
	gets reset to current_timers.end() again.
	*/
	proc_timerObj(
		time_t time_start,
		time_t time_end);

	/*! Destructor

	The destructor removes this from current_timers if my_iter is not
	current_timers.end().

	*/
	~proc_timerObj();

	/*! Iterator into current_timers

	This is set when install_timer() is called and cleared when the timer
	triggers.
	*/
	current_timers_t::iterator my_iter;

	const time_t time_start;
	const time_t time_end;

	//! Trigger the timer
	virtual void triggered()=0;

	//! Return the timer description, for log messages.
	virtual std::string description() const=0;

	//! Update the timer containers
	virtual void update_timer_containers(const current_containers &);
};

#endif
