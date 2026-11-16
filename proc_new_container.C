/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "parsed_yaml.H"
#include "proc_new_container.H"

proc_new_containerObj::proc_new_containerObj(
	const std::string &name
) : new_container{std::make_shared<proc_containerObj>(name)}
{
}

bool proc_new_containerObj::is_valid(
	const std::function<void (const std::string_view &name,
		const std::string_view &error_message)
	> &error) const
{
	for (const auto &dependency_list : all_dependencies)
	{
		for (auto &existing_dependency : this->*dependency_list)
		{
			if (!proc_validpath(existing_dependency))
			{
				error(
					existing_dependency,
					"invalid dependency"
				);

				return false;
			}
		}
	}

	return new_container->is_valid(error);
}
