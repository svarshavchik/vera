/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef log_h
#define log_h

#include <string>
#include <functional>
#include <time.h>

#include "proc_container_statefwd.H"
#include "proc_containerfwd.H"

//! Record a change in a container's state
void log_state_change(const proc_container &,
		      const proc_container_state &);

/*! Record a change to a container's state in the switch log

  If there's a switchlog open, the container's state change gets logged
  to the switch log.
*/

void log_state_change_to_switchlog(const std::string &name,
				   const std::string_view &new_container_state);

//! Container's starting process failed
void log_container_failed_process(const proc_container &, int);

//! Record a system error related to a container
void log_container_error(const proc_container &, const std::string &);

//! Log a message related to a container
void log_container_message(const proc_container &, const std::string &);

//! Log container's output
void log_container_output(const proc_container &, const std::string &);

//! Log a generic message
void log_message(const std::string &);

/*!  Return current clock time

  This is not the system time. This is CLOCK_MONOTONIC_COARSE, and this is
  cached. The value of CLOCK_MONOTONIC_COARSE is retrieved after waking up
  for an event, and it remains cached until the next wakeup.

 */

const struct timespec &log_current_timespec();

//! Timeout for SIGTERM to do its work

#define SIGTERM_TIMEOUT 10

//! At startup, this points to real syslog or to standard output

extern void (*log_to_syslog)(int level,
			     const char *program,
			     const char *message);

//! Format an interval as minutes+seconds. The interval should be fairly short.

std::string log_elapsed(time_t n);

#endif
