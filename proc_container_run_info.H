/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef proc_container_run_info_h
#define proc_container_run_info_h

#include <type_traits>
#include <functional>

#include "proc_container_state.H"

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
};

#endif
