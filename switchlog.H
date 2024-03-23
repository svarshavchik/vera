/*
** Copyright 2024 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef switchlog_h
#define switchlog_h

#include <iostream>
#include "log.H"

//! Start logging a runlevel switch
void switchlog_start(const std::string &new_runlevel);

//! Internal function to open a stream for the current switchlog
void switchlog_start();

/*!
  Internal function to get the switchlog's current file descriptor.

  A non-nullptr indicates that we're logging a runlevel switch.
  Overridden in unit tests.
*/

std::ostream *get_current_switchlog();

//! Stop logging a runlevel switch, if we're logging it.
void switchlog_stop();

#define FORMAT_TIMESPEC(tv) \
	(tv).tv_sec << "." << std::setw(9) << std::setfill('0') << (tv).tv_nsec
#endif
