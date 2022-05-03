/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef proc_container_h
#define proc_container_h

#include <string>
#include <unordered_set>
#include <time.h>
#include <functional>
#include <variant>
#include <type_traits>
#include <libintl.h>
#include <memory>

//! A process container that's not running
struct state_stopped {

	//! Human readable label.
	operator std::string() const;
};

//! A process container that's starting
struct state_starting {

	//! Human readable label.
	operator std::string() const;
};

//! A process container that's started
struct state_started {

	//! Human readable label.
	operator std::string() const;
};

//! A proess container that's stopping
struct state_stopping {

	//! Human readable label.
	operator std::string() const;
};

//! The state of a process container

//! - \c state_stopped - the container is stopped
//!
//! - \c state_starting - the container is starting
//!
//! - \c state_started - the container has started
//!
//! - \c state_stopping - the container is stopping

typedef std::variant<state_stopped,
		     state_starting,
		     state_started,
		     state_stopping> proc_container_state;

//! Container type

enum class proc_container_type {
	loaded,		//!< Loaded process container
	synthesized	//!< Synthesized, for dependencies.
};

class proc_containerObj;

/*! A process container

Once constructed, the container object is immutable.

*/

typedef std::shared_ptr<const proc_containerObj> proc_container;

/*! Helper class used by proc_container_set

Implements a transparent hash function and equality comparison.

Allows proc_container_set to be looked up by a string

*/

template<typename T, typename=void>
struct get_name_for_comparator {

	template<typename V>
	static auto & name(V &&v)
	{
		return v;
	}
};

/*! Helper class used by proc_container_set

Implements a transparent hash function and equality comparison.

Allows proc_container_set to be looked up by a proc_container/

*/

template<typename T>
struct get_name_for_comparator<T, std::void_t<decltype(std::declval<std::remove_cvref_t<T> &&>()->name)>> {

	template<typename V>
	static auto &name(V &&v)
	{
		return v->name;
	}

};

//! Transparent comparator for proc_container objects.

struct proc_container_equal {

	typedef void is_transparent;

	template<typename T, typename U>
	bool operator()(T &&t, U &&u) const noexcept
	{
		return get_name_for_comparator<T>::name(
			std::forward<T>(t)
		) == get_name_for_comparator<U>::name(
			std::forward<U>(u)
		);
	}
};

//! Transparent hash function for for proc_container objects.

struct proc_container_hash {

	typedef void is_transparent;

	template<typename T>
	auto operator()(T &&t) const noexcept
	{
		return std::hash<std::string>{}(
			get_name_for_comparator<T>::name(
				std::forward<T>(t)
			)
		);
	}
};

//! A set of process containers.

typedef std::unordered_set<proc_container, proc_container_hash,
			   proc_container_equal> proc_container_set;

//! Install process containers

void proc_containers_install(const proc_container_set &new_containers);

//! Expose the currently installed process containers.

void get_proc_containers(const std::function<void (const proc_container &,
						   const proc_container_state &)
			 > &cb);

proc_container_set current_proc_containers();

//! Attempt to start a container.
std::string proc_container_start(const std::string &s);

//! Attempt to stop a container.
std::string proc_container_stop(const std::string &s);


//! Implementation object.

//! \see proc_container

class proc_containerObj {

public:
	//! The container's name
	std::string name;

	//! Its type
	proc_container_type type=proc_container_type::loaded;

	//! Constructor
	proc_containerObj();

	//! Destructor
	~proc_containerObj();
};

#endif
