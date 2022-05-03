/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef proc_container_runnerfwd_h
#define proc_container_runnerfwd_h

#include <memory>

class proc_container_runnerObj;

/*!

A running process in a process container.

*/

typedef std::shared_ptr<const proc_container_runnerObj> proc_container_runner;

#endif
