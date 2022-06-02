/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef proc_container_run_info_h
#define proc_container_run_info_h

#include <type_traits>
#include <functional>
#include <optional>

#include "proc_container_state.H"
#include "proc_container_group.H"

#include <iostream>

struct proc_container_run_info {
	//! The actual state
	proc_container_state state;

	//! This container has been completely removed

	//! From the system configuration, that is. We need to wait until
	//! all processes are stopped, that the actual container gets removed
	//! and this object gets deleted.
	bool autoremove=false;

	//! Non-default non-copy constructor gets forwarded to state's.
	template<typename T,
		 typename=std::enable_if_t<!std::is_same_v<
						   std::remove_cvref_t<T>,
						   proc_container_run_info>>>
	proc_container_run_info(T &&t) : state{std::forward<T>(t)} {}

	//! Default constructor

	proc_container_run_info()=default;

	//! Destructor
	~proc_container_run_info()=default;

	/*! Move assignment operator

	  install() updates the list of managed containers, creating
	  new_current_containers, then moves the existing containers'
	  run state to the new_current_containers.
	 */
	proc_container_run_info &operator=(proc_container_run_info &&)=default;

	/*!
	  Move constructor

	  Based on the move-assignment operator.
	*/
	proc_container_run_info(proc_container_run_info &&);


	//! Do something if the process container is in a specific state.

	//! If so, invoke the passed-in callable object with the state as
	//! a parameter.

	template<typename T, typename Callable>
	void run_if(Callable &&callable)
	{
		std::visit([&]
			   (auto &current_state)
		{
			typedef std::remove_cvref_t<decltype(current_state)
						    > current_state_t;

			if constexpr(std::is_same_v<
				     current_state_t,
				     T>) {
				callable(current_state);
			}
		}, state);
	}

	template<typename T, typename Callable>
	void run_if(Callable &&callable) const
	{
		std::visit([&]
			   (auto &current_state)
		{
			typedef std::remove_cvref_t<decltype(current_state)
						    > current_state_t;

			if constexpr(std::is_same_v<
				     current_state_t,
				     T>) {
				callable(current_state);
			}
		}, state);
	}

	//! Do something if the process container is in a specific state.

	//! If so, invoke the 1st callable object with the state as
	//! a parameter.
	//!
	//! Invoke the 2nd callable if the container is in some other state.

	template<typename T, typename Callable1, typename Callable2>
	void run_if(Callable1 &&callable1, Callable2 && callable2)
	{
		std::visit([&]
			   (auto &current_state)
		{
			typedef std::remove_cvref_t<decltype(current_state)
						    > current_state_t;

			if constexpr(std::is_same_v<
				     current_state_t,
				     T>) {
				callable1(current_state);
			}
			else
			{
				callable2();
			}
		}, state);
	}

	//! Active cgroup

	//! Object that tracks the cgroup.
	std::optional<proc_container_group> group;

	//! When containers get updated, make a note of it.

	void updated(const proc_container &pc);

	//! Determine whether this container's state is transferrable

	//! Before re-execing ourselves each container gets checked.
	//! is_transferrable() gets called to capture the container's
	//! serialized metadata.
	//!
	//! Only started and stopped containers can be transferrable,
	//! and the started container cannot have a reload_or_restart
	//! runner. If this is the case: writes the container's state
	//! to the given stream and returns true.
	//!
	//! Returns false if the container's state cannot be saved.

	bool is_transferrable(const proc_container &pc,
			      std::ostream &o);

	//! About to re-exec ourselves.

	//! All containers are transferrable. This removes the close-on
	//! exec flag on any file descriptors here.

	void prepare_to_transfer(const proc_container &pc);

	//! We are reexeced. Restore ourselves.

	//! Receives the input stream for the output stream that was passed
	//! to is_transferable.

	void restored(std::istream &i,
		      const group_create_info &create_info,
		      const std::function<void (state_started &,
						pid_t)> &reinstall_respawn_runner
	);

	//! After all containers were restored, this gets called for them.

	void all_restored(const group_create_info &create_info);
};

#endif
