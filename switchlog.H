/*
** Copyright 2024 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef switchlog_h
#define switchlog_h

#include <iostream>
#include <fstream>
#include <functional>
#include <filesystem>
#include <vector>
#include <functional>
#include "log.H"
#include "proc_container_statefwd.H"

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

//! Clean up and purge the switchlog directory

void switchlog_purge(const char *directory,
		     unsigned ndays,
		     std::function<void (std::string)> log_error);

//! Create a new switchlog
void switchlog_create(const char *directory, std::ofstream &o);

//! Save the switchlog
void switchlog_save(const char *directory,
		     std::function<void (std::string)> log_error);

//! Switchlogs returned from switch_enumerate()

struct enumerated_switchlog {

	//! Pathname to the switchlog
	std::filesystem::path filename;

	//! What the switchlog is doing (extracted from the 1st line)
	std::string switchname;

	//! Timestamp when the log finished
	time_t log_end;
};

/*! Enumerate available switchlogs

  Returns a sorted list, sorted by the log_end timestamp.
*/

std::vector<enumerated_switchlog> enumerate_switchlogs(const char *directory);

//! Analyzed switchlog

struct analyzed_switchlog {

	//! A log entry for a single container
	struct container {

		//! Container's name
		std::string name;

		//! This is going to be either STATE_STARTED or STATE_STOPPED
		const char *label;

		//! How long this container waited to start or stop
		elapsed_time waiting;

		//! How long it took this container to start or stop
		elapsed_time elapsed;
	};

	//! The analyzed switch log
	std::vector<container> log;
};

/*! Analyze a switchlog */

analyzed_switchlog switchlog_analyze(const enumerated_switchlog &log);

#endif
