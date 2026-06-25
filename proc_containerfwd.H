/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef proc_containerfwd_h
#define proc_containerfwd_h

#include <memory>
#include <concepts>
#include <string>
#include <string_view>

class proc_containerObj;

/*! A process container

Once constructed, the container object is immutable.

*/

typedef std::shared_ptr<const proc_containerObj> proc_container;

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

/*! Concept used by get_name_for_comparator specialization

*/

template <typename T> concept has_new_container_field = requires(T ptr) {
	{ ptr->new_container->name } -> std::convertible_to<std::string_view>;
};

/*! Helper class used by proc_new_container_set

Implements a transparent hash function and equality comparison.

Allows proc_new_container_set to be looked up by a proc_new_container.

*/

template<has_new_container_field T>
struct get_name_for_comparator<T> {

	template<typename V>
	static auto &name(V &&v) noexcept
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
	size_t operator()(T &&t) const noexcept
	{
		return std::hash<std::string>{}(
			get_name_for_comparator<T>::name(
				std::forward<T>(t)
			)
		);
	}
};

#endif
