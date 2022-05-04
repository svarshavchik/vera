/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef log_h
#define log_h

#include <string>
#include "proc_container.H"

void log_state_change(const proc_container &,
		      const proc_container_state &);

void log_container_failed_process(const proc_container &, int);

void log_container_error(const proc_container &, const std::string &);

#endif
