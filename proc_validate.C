/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/

#include "config.h"
#include "proc_loader.H"
#include "proc_new_container.H"
#include <set>

bool proc_validate_and_dump(
	const std::string &unitfile,
	const std::string &relativepath_override,
	const std::string &config_global,
	const std::string &config_local,
	const std::string &config_override,
	const std::function<void (const std::string &)> &log_message,
	const std::function<void (const loaded_containers &)> &dump
)
{
	std::ifstream i{unitfile};

	if (!i)
	{
		std::cerr << unitfile << ": " << strerror(errno)
			  << std::endl;
		return false;
	}

	std::cout << _("Loading: ") << unitfile << "\n";

	size_t n=unitfile.rfind('/');

	std::filesystem::path relative_path;

	try {

		relative_path=unitfile.substr(n == unitfile.npos
					      ? 0:n+1);

		if (!relativepath_override.empty())
			relative_path=relativepath_override;
	} catch (...) {
		std::cerr << "Invalid filename" << std::endl;
		return false;
	}

	proc_new_container_set set, new_configs;
	bool error=false;

	try {
		proc_override o;

		o.set_state(proc_override::state_t::enabled);

		auto results=proc_load(i, unitfile, relative_path, o,
			      [&]
			      (const auto &msg)
			      {
				      std::cerr << msg << std::endl;
				      error=true;
			      });

		set=std::move(results);

		std::cout << _("Loading installed units") << std::endl;

		auto current_configs=proc_load_all(
			config_global,
			config_local,
			config_override,
			[&]
			(const std::string &warning_message)
			{
				log_message(warning_message);
			},
			[&]
			(const std::string &error_message)
			{
				error=true;
				log_message(error_message);
			});

		// Take what's validated, and add in the current_configs
		// but what's validated takes precedence, so any
		// existing configs that are in the new set get
		// ignored.

		new_configs=set;
		new_configs.insert(current_configs.containers.begin(),
				   current_configs.containers.end());
	} catch (...) {
		std::cerr << unitfile << _(": parsing error")
			  << std::endl;
		return false;
	}

	if (error)
		return false;

	loaded_containers lc{set};
	dump(lc);

	for (auto &s:set)
	{
		static constexpr struct {
			const char *dependency;
			std::unordered_set<std::string>
			proc_new_containerObj::*names;
		} dependencies[]={
			{"requires",
			 &proc_new_containerObj::dep_requires},
			{"requires-first",
			 &proc_new_containerObj::dep_requires_first},
			{"required-by",
			 &proc_new_containerObj::dep_required_by},
			{"starting: before",
			 &proc_new_containerObj::starting_before},
			{"starting: after",
			 &proc_new_containerObj::starting_after},
			{"stopping: before",
			 &proc_new_containerObj::stopping_before},
			{"stopping: after",
			 &proc_new_containerObj::stopping_after},
		};

		if (!s->new_container->alternative_group.empty())
		{
			if (!s->dep_required_by.empty())
				log_message(
					_("Alternative-Group container "
					  "with a required-by dependency: "
					) + s->new_container->name
				);
		}

		for (auto &[dep_name, ptr] : dependencies)
		{
			auto &us=(*s).*(ptr);
			std::set<std::string> sorted{
				us.begin(),
				us.end()
			};

			if (sorted.find(s->new_container->name) != sorted.end())
			{
				log_message(
					_("Circular dependency found: ")
					+ s->new_container->name
					+ ": " + dep_name);
			}
			for (auto &name:sorted)
			{
				auto iter=new_configs.find(name);

				if (iter != new_configs.end())
					continue;

				if (name.substr(0, sizeof(RUNLEVEL_PREFIX)-1)
				    == RUNLEVEL_PREFIX)
					continue;

				std::cout << _("Warning: ")
					  << s->new_container->name
					  << "("
					  << dep_name
					  << "): "
					  << name
					  << _(": not defined")
					  << std::endl;
			}
		}
	}

	for (auto &s:new_configs)
	{
		for (auto &r:s->dep_requires)
		{
			auto iter=set.find(r);

			if (iter == set.end())
				continue;

			if (!(*iter)->new_container->alternative_group.empty())
			{
				log_message(_("Container with a dependency on"
					      " an Alternative-Group: ")
					    + s->new_container->name);
			}
		}
	}

	return true;
}

bool proc_validate(const std::string &unitfile,
		   const std::string &relativepath_override,
		   const std::string &config_global,
		   const std::string &config_local,
		   const std::string &config_override,
		   const std::function<void (const std::string &)> &log_message)
{
	return proc_validate_and_dump(
		unitfile,
		relativepath_override,
		config_global,
		config_local,
		config_override,
		log_message,
		proc_load_dump
	);
}
