/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef proc_containerfwd_h
#define proc_containerfwd_h

#include "uid_sessionid.H"
#include <memory>
#include <concepts>
#include <string>
#include <string_view>
#include <unordered_set>

class proc_containerObj;

/*! A process container

Once constructed, the container object is immutable.

*/

typedef std::shared_ptr<const proc_containerObj> proc_container;
//! A set of process containers.

/*! Helper class used by proc_container_set

Implements a transparent hash function and equality comparison.

Allows proc_container_set to be looked up by a string

*/

template<typename T>
struct get_name_for_comparator {

	template<typename V>
	static auto &name(V &&v)
	{
		return v;
	}
};

/*! Concept used by get_name_for_comparator specialization

*/

template<typename T>
concept has_name_field = requires(T ptr) {
	{ ptr->name } -> std::convertible_to<std::string_view>;
};

/*! Helper class used by proc_container_set

Implements a transparent hash function and equality comparison.

Allows proc_container_set to be looked up by a proc_container.

*/

template<has_name_field T>
struct get_name_for_comparator<T> {

	template<typename V>
	static auto &name(V &&v) noexcept
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

//! Transparent comparator for proc_container objects.

struct proc_container_less_than {

	typedef void is_transparent;

	template<typename T, typename U>
	bool operator()(T &&t, U &&u) const noexcept
	{
		return get_name_for_comparator<T>::name(
			std::forward<T>(t)
		) < get_name_for_comparator<U>::name(
			std::forward<U>(u)
		);
	}
};

//! Transparent hash function for for proc_container objects.

struct proc_container_hash {

	typedef void is_transparent;

	template<typename T, typename defer=void>
	size_t operator()(T &&t) const noexcept
	{
		return std::hash<std::string>{}(
			get_name_for_comparator<T>::name(
				std::forward<T>(t)
			)
		);
	}
};

typedef std::unordered_set<proc_container, proc_container_hash,
			   proc_container_equal> proc_container_set;

//! Container installation mode.

struct container_install {

	/*! Updated list of containers

	 Replaces the current set of containers.
	 */

	struct update {};

	/*! Initial list of containers

	  This also checks if we have been re-execed, and if so restores
	  the existing containers' state.
	*/
	struct initial {};

	/*!
	  Start a new session.
	*/

	struct new_session {
		active_session_t new_active_session;
	};
};

typedef std::variant<
	container_install::update,
	container_install::initial,
	container_install::new_session
> container_install_t;

#endif
