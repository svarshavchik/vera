/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef proc_new_container_h
#define proc_new_container_h

#include "proc_new_containerfwd.H"
#include "proc_container.H"
#include <array>
#include <string>
#include <unordered_set>

typedef std::shared_ptr<proc_new_containerObj> proc_new_container;

typedef std::unordered_set<proc_new_container, proc_container_hash,
			   proc_container_equal> proc_new_container_set;

//! Dependencies for a new container.

//! Used when installing a new set of containers. Includes additional data
//! that's not needed after the containers get processed and installed.

class proc_new_containerObj {

public:
	//! Constructor
	proc_new_containerObj(const std::string &name);

	//! The new container

	const std::shared_ptr<proc_containerObj> new_container;

	//! What other process containers this one requires
	std::unordered_set<std::string> dep_requires;

	//! What others this one requires, and must be started first.
	std::unordered_set<std::string> dep_requires_first;

	//! What other process containers require this one.
	std::unordered_set<std::string> dep_required_by;

	//! These process containers start before this one.
	std::unordered_set<std::string> starting_before;

	//! These process containers start after this one.
	std::unordered_set<std::string> starting_after;

	//! These process containers stop before this one.
	std::unordered_set<std::string> stopping_before;

	//! These process containers stop after this one.
	std::unordered_set<std::string> stopping_after;

	/*! Verify validity of all container paths */
	bool is_valid(
		const std::function<void (const std::string_view &name,
			const std::string_view &error_message)
		> &error) const;

	//! All dependency sets, for convenience
	static inline constexpr std::array<
		std::unordered_set<std::string> proc_new_containerObj::*, 7
	> all_dependencies={
		&proc_new_containerObj::dep_requires,
		&proc_new_containerObj::dep_requires_first,
		&proc_new_containerObj::dep_required_by,
		&proc_new_containerObj::starting_before,
		&proc_new_containerObj::starting_after,
		&proc_new_containerObj::stopping_before,
		&proc_new_containerObj::stopping_after
	};
};

#endif
