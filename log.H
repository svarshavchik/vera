/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef log_h
#define log_h

#include <string>
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

//! Timeout for SIGTERM to do its work

#define SIGTERM_TIMEOUT 10

//! At startup, this points to real syslog or to standard output

extern void (*log_to_syslog)(int level,
			     const char *program,
			     const char *message);

#endif
