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

void log_state_change(const proc_container &,
		      const proc_container_state &);

void log_container_failed_process(const proc_container &, int);

void log_container_error(const proc_container &, const std::string &);

void log_container_message(const proc_container &, const std::string &);

void log_message(const std::string &);

time_t log_current_time();
const struct timespec &log_current_timespec();

//! Timeout for SIGTERM to do its work

#define SIGTERM_TIMEOUT 10

//! At startup, this points to real syslog or to standard output

extern void (*log_to_syslog)(int level,
			     const char *program,
			     const char *message);

//! Format an interval as minutes+seconds. The interval should be fairly short.

std::string log_elapsed(time_t n);

/*! Extract a container's elapsed time of starting/stopping.

Given the current time and a container, determine how long it's been
starting or stopping for, and whether it's been starting or stopping.

Invokes callbacks:

- how many seconds it's been starting

- how many seconds it's been starting and it's timeout.
*/

std::string get_state_and_elapsed_for(
	const proc_container_state &state,
	time_t current_time,

	/*!
	  Invoked to report how many seconds it's been
	  starting or stopping for
	*/

	const std::function<void (time_t)> &,

	/*!
	  Invoked to report how many seconds it's been
	  starting or stopping and its timeout.
	*/

	const std::function<void (time_t, time_t)> &);
#endif
