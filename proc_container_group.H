/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef proc_container_group_h
#define proc_container_group_h

#include "poller.H"
#include "proc_containerfwd.H"
#include <tuple>
#include <string>

/*! Movable POD for a proc_container_group.

proc_container_group implements move semantics. This POD is the movable
data
*/


struct proc_container_group_data {

	/*!
	  Pipe attached to the processes' standard output and error.

	  Both the read and the write end of the pipe exist, and the write
	  end gets attached to the process after it gets forked.

	*/

	int stdouterrpipe[2]={-1, -1};

	//! The poller for the read end of the stdouterrpipe

	polledfd stdouterrpoller;

	//! The container this group is for.

	proc_container container;

	static const char *get_cgroupfs_base_path();

	//! Return the cgroups directory.

	std::string cgroups_dir();

	//! Create a cgroups directory and return an fd to its cgroup.events

	//! And its full pathname.
	std::tuple<int, std::string> cgroups_dir_create();

	//! cgroup.events file descriptor
	int cgroup_eventsfd{-1};

	//! The cgroup_eventsfd poller

	inotify_watch_handler cgroup_eventsfdhandler;

	//! Register the forked process in the cgroup.

	void cgroups_register();
};

struct group_create_info;

/*!

An active system process container group


 */

class proc_container_group : proc_container_group_data {

public:
	proc_container_group()=default;

	~proc_container_group();

	proc_container_group(proc_container_group &&);

	/*! Initialize the new container group


	  A default constructor and an explicit create() is intentional.
	  If create() can't do something it might end up still allocating
	  partial resources, and the destructor will clean it up.

	 */

	bool create(const group_create_info &);

	/*! This is a new child process

	  Move it into the container group, and redirect the standard output
	  and error to the given file descriptor.
	 */
	bool forked(int fd);

	proc_container_group &operator=(const proc_container_group &)=delete;

	proc_container_group &operator=(proc_container_group &&);

	//! Try to rmdir my cgroups directory.
	bool cgroups_try_rmdir();

	//! Send a signal to all processes in the group

	void cgroups_sendsig(int);
};

#endif
