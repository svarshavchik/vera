/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef proc_new_containerfwd_h
#define proc_new_containerfwd_h

#include <string_view>
#include <concepts>
#include "proc_containerfwd.H"

class proc_new_containerObj;

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

#endif
