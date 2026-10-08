/*
** Copyright 2022-2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "configdirs.h"
#include "proc_loader.H"
#include "vvconfigdirs.H"

std::string environconfig()
{
	return ENVIRONCONFIG;
}

std::string installconfigdir()
{
	return INSTALLCONFIGDIR;
}

// Local overrides of unit specification files

std::string localconfigdir()
{
	return LOCALCONFIGDIR;
}

// Overrides

std::string overrideconfigdir()
{
	return OVERRIDECONFIGDIR;
}

std::string runlevelconfig()
{
	return RUNLEVELCONFIG;
}

std::tuple<runlevels, bool> load_runlevelconfig()
{
	auto rl=proc_get_runlevel_config(
		runlevelconfig(),
		[]
		(const std::string &error)
		{
			log_message(
				runlevelconfig() +
				_(": unable to read or parse, "
				  "using built-in default"));
		});

	bool overridden=proc_apply_runlevel_override(rl);

	return std::tuple{std::move(rl), overridden};
}
