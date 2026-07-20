/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef proc_timerfwd_h
#define proc_timerfwd_h

#include <memory>

class proc_timerObj;

/*! A scheduled timeout.

create_timer() returns a handle for the timer. run_timers() checks for
expired timers.

*/

typedef std::shared_ptr<const proc_timerObj> proc_timer;

//! Check and invoke timed out timers.

//! Returns a timeout before run_timers() should be checked again, for
//! a possible timer expiration.

int run_timers();

//! Whether there are any pending timers to be run.

std::string has_timers();

//! Install a new timer

void install_timer(const std::shared_ptr<proc_timerObj> &t);

#endif
