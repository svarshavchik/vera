/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef proc_container_group_h
#define proc_container_group_h

#include "poller.H"
#include "proc_containerfwd.H"
#include "proc_loaderfwd.H"
#include "privrequest.H"
#include <tuple>
#include <string>
#include <string_view>
#include <vector>
#include <signal.h>

struct group_create_info;

/*! Movable POD for a proc_container_group.

proc_container_group implements move semantics. This POD is the movable
data.

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

	//! Return the cgroups directory for this container.

	std::string cgroups_dir() const;

	//! Return ths cgroups directory for the given container

	static std::string cgroups_dir(std::string name);

	//! Create a cgroups directory.

	bool cgroups_dir_create();

	//! Path to the cgroups.events file

	std::string cgroup_events();

	//! The cgroup_eventsfd poller

	inotify_watch_handler cgroup_eventsfdhandler;

	//! Whether, as of the last report, this container had processes

	bool populated=false;

	//! Register the forked process in the cgroup.

	bool cgroups_register();

	//! Install the poller and the event handler.

	//! Called from create().

	bool install(
		const std::string &cgroup_events_path,
		const group_create_info &create_info
	);

	//! Internal function that parses cgroups.events

	static bool is_populated(
		//! cgroup.events file descriptor
		int fd,

		//! Scratch buffer used for reading.
		std::string &scratch_buffer
	);

	void log_output(const proc_container &pc,
			const external_filedesc &requester_stdout);

private:

	//! Buffer used by log_output to collect captured output.

	std::string buffer;
};

/*!

An active system process container group


 */

class proc_container_group : proc_container_group_data {

	struct cgroup_procs_file;
public:
	proc_container_group()=default;

	~proc_container_group();

	proc_container_group(proc_container_group &&);

	/*! Initialize the new container group


	  A default constructor and an explicit create() is intentional.
	  If create() can't do something it might end up still allocating
	  partial resources, and the destructor will clean it up.

	 */

	bool create(const group_create_info &,
		    const proc_override::resources_t &);

	/*! This is a new child process

	  Move it into the container group, and redirect its standard output
	  and error.
	 */
	bool forked();

	proc_container_group &operator=(const proc_container_group &)=delete;

	proc_container_group &operator=(proc_container_group &&);

	//! Try to rmdir my cgroups directory.
	bool cgroups_try_rmdir(const proc_container &pc,
			       const external_filedesc &requester_stdout);

	//! Send a signal to all processes in the group

	void cgroups_sendsig_all(int);

	//! Send a signal to all processes except children with same exes.

	void cgroups_sendsig_parents(int);

	//! Freeze it
	bool freeze_thaw(std::string_view);
private:

	//! Recursive implementation function.
	void cgroups_sendsig_parents(
		const container_state_info::hier_pids &pids,
		const container_state_info::pid_info *parent,
		int sig);

public:
	//! Send a signal to a pid

	//! Stubbed out in unit tests.

	static void cgroups_sendsig(pid_t p, int sig);

	//! Return a list of processes in this container group
	//!
	//! The child_only flag returns only those processes that do not have
	//! a parent process in the same container group.

	std::vector<pid_t> cgroups_getpids(bool child_only) const;

	//! Return a list of processes in the given container group
	//!
	//! The child_only flag returns only those processes that do not have
	//! a parent process in the same container group.

	static std::vector<pid_t> cgroups_getpids(
		const std::string &name,
		bool child_only
	);

	//! Record an updated container definition.

	void updated(const proc_container &pc)
	{
		container=pc;
	}

	using proc_container_group_data::populated;

	using proc_container_group_data::log_output;
	///////////////////////////////////////////////////////////////////
	//
	// Saving and restoring state for a re-exec.

	//! Serialize our open file descriptors into the given output stream.
	void save_transfer_info(std::ostream &);

	//! Everything has been serialized, remove their close-on-exec flag.
	void prepare_to_transfer();

private:
	//! Handle transferring for a given file descriptor

	//! Implemented differently in unit tests.

	static void prepare_to_transfer_fd(int &);
public:
	//! Restore the serialized file descriptors, set their close-on-exec.

	//! Called in lieu of create() after a re-exec. Returns false if there
	//! was a problem.

	bool restored(std::istream &,
		      const group_create_info &create_info);

	//! Called after all containers are restored.

	void all_restored(const group_create_info &create_info);
};

#endif
