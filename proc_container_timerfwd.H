/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef proc_container_timerfwd_h
#define proc_container_timerfwd_h

#include <memory>

class proc_container_timerObj;

/*! A scheduled timeout.

create_timer() returns a handle for the timer. run_timers() checks for
expired timers.

*/

typedef std::shared_ptr<const proc_container_timerObj> proc_container_timer;

//! Check and invoke timed out timers.

//! Returns a timeout before run_timers() should be checked again, for
//! a possible timer expiration.

int run_timers();


#endif
