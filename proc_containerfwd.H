/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef proc_containerfwd_h
#define proc_containerfwd_h

#include <memory>

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

Allows proc_container_set to be looked up by a proc_container.

*/

template<typename T>
struct get_name_for_comparator<T, std::void_t<decltype(std::declval<std::remove_cvref_t<T> &&>()->name)>> {

	template<typename V>
	static auto &name(V &&v)
	{
		return v->name;
	}

};

/*! Helper class used by proc_new_container_set

Implements a transparent hash function and equality comparison.

Allows proc_new_container_set to be looked up by a proc_new_container.

*/

template<typename T>
struct get_name_for_comparator<T, std::void_t<decltype(std::declval<std::remove_cvref_t<T> &&>()->new_container->name)>> {

	template<typename V>
	static auto &name(V &&v)
	{
		return v->new_container->name;
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
	auto operator()(T &&t) const noexcept
	{
		return std::hash<std::string>{}(
			get_name_for_comparator<T>::name(
				std::forward<T>(t)
			)
		);
	}
};

#endif
