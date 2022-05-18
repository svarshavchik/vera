/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef proc_container_group_h
#define proc_container_group_h

#include "poller.H"

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

	polledfd stdouterrpoller;
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

	  Move it into the container group.
	 */
	bool forked();

	proc_container_group &operator=(const proc_container_group &)=delete;

	proc_container_group &operator=(proc_container_group &&);
};

#endif
