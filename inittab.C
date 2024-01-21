/*
** Copyright 2024 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "inittab.H"
#include "log.H"
#include "yaml_writer.H"
#include "proc_loader.H"
#include <algorithm>
#include <set>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace {
#if 0
}
#endif

/*!
  A new inittab entry being imported.
*/

struct inittab_entry {

	/*!
	  Previous inittab command
	*/
	std::string &prev_command;

	/*! First field in the inittab line
	 */
	std::string identifier;

	/*! The last file in the inittab line

	  It becomes the starting command.
	*/

	std::string starting_command;

	/*! The descrption that goes into the generated unit file */
	std::string description;

	/* Unit's starting type */
	const char *start_type="oneshot";

	/* Unit's stop type */
	const char *stop_type="manual";

	// Unit or units where this inittab/namespace entry starts in.
	std::vector<std::string> required_by;

	// Additional dependencies
	std::vector<std::string> starts_before, starts_after,
		stops_before, stops_after;

	//! This unit starts in this runlevel.

	void required_by_runlevel(const std::string &runlevel)
	{
		required_by.push_back("/system/"+runlevel);
	}

	//! New entry read from /etc/inittab

	inittab_entry(std::string &prev_command,
		      std::string identifier,
		      std::string starting_command)
		: prev_command{prev_command},
		  identifier{std::move(identifier)},
		  starting_command{std::move(starting_command)},
		  description{this->identifier + ": "
		+ this->starting_command}
	{
		// If there's a previous command this one will start after it
		// and stop before it.

		if (!prev_command.empty())
		{
			starts_after.push_back(prev_command);
			stops_before.push_back(prev_command);
		}
	}

	//! Additional entries generated.

	inittab_entry(inittab_entry &prev_entry,
		      std::string identifier,
		      std::string starting_command)
		: inittab_entry{
				prev_entry.prev_command,
				std::move(identifier),
				std::move(starting_command)
			}
	{
		description=prev_entry.description;
	}

	//! Convert this inittab entry to YAML.
	yaml_write_map create() const;
};

yaml_write_map inittab_entry::create() const
{
	// Convert the fields to YAML, one by one.

	yaml_map_t unit;

	unit.emplace_back(
		std::make_shared<yaml_write_scalar>("name"),
		std::make_shared<yaml_write_scalar>(identifier)
	);
	unit.emplace_back(
		std::make_shared<yaml_write_scalar>("description"),
		std::make_shared<yaml_write_scalar>(description)
	);

	if (!required_by.empty())
	{
		unit.emplace_back(
			std::make_shared<yaml_write_scalar>("required-by"),
			std::make_shared<yaml_write_seq>(
				required_by.begin(), required_by.end()
			)
		);
	}

	yaml_map_t starting, stopping;

	starting.emplace_back(
		std::make_shared<yaml_write_scalar>("type"),
		std::make_shared<yaml_write_scalar>(start_type)
	);

	stopping.emplace_back(
		std::make_shared<yaml_write_scalar>("type"),
		std::make_shared<yaml_write_scalar>(stop_type)
	);

	if (!starting_command.empty())
	{
		starting.emplace_back(
			std::make_shared<yaml_write_scalar>("command"),
			std::make_shared<yaml_write_scalar>(starting_command)
		);
	}

	if (!starts_after.empty())
		starting.emplace_back(
			std::make_shared<yaml_write_scalar>("after"),
			std::make_shared<yaml_write_seq>(
				starts_after.begin(), starts_after.end()
			));

	if (!starts_before.empty())
		starting.emplace_back(
			std::make_shared<yaml_write_scalar>("before"),
			std::make_shared<yaml_write_seq>(
				starts_before.begin(), starts_before.end()
			));

	if (!stops_before.empty())
		stopping.emplace_back(
			std::make_shared<yaml_write_scalar>("before"),
			std::make_shared<yaml_write_seq>(
				stops_before.begin(), stops_before.end()
			));

	if (!stops_after.empty())
		stopping.emplace_back(
			std::make_shared<yaml_write_scalar>("after"),
			std::make_shared<yaml_write_seq>(
				stops_after.begin(), stops_after.end()
			));

	unit.emplace_back(
		std::make_shared<yaml_write_scalar>("starting"),
		std::make_shared<yaml_write_map>(
			std::move(starting)
		)
	);

	unit.emplace_back(
		std::make_shared<yaml_write_scalar>("stopping"),
		std::make_shared<yaml_write_map>(
			std::move(stopping)
		)
	);

	prev_command=identifier;

	unit.emplace_back(
		std::make_shared<yaml_write_scalar>("Version"),
		std::make_shared<yaml_write_scalar>("1")
	);

	return yaml_write_map{std::move(unit)};
}

//! Orderly process for converting /etc/inittab to units

struct convert_inittab {

	//! Which directory we'll write the units into

	const std::string &unit_directory;

	//! Whether an error occured.

	bool error=false;

	/*
	  All -start scripts we generated, for each runlevel.

	  We expect only one per runlevel.
	*/
	std::map<std::string, inittab_entry> all_start_scripts;

	/*
	  Use the runlevels configuration to map short runlevel
	  codes, like '4' into log names, like 'multi-user'.
	*/

	std::unordered_map<std::string, std::string> runlevel_lookup;

	// Sanity check: unique identifiers
	std::unordered_set<std::string> ids_seen;

	std::string prev_command;

	// Keep track of runlevels that run rc.? scripts
	std::set<std::string> all_single_multi_runlevels;

	convert_inittab(const std::string &unit_directory,
			const runlevels &runlevels)
		: unit_directory{unit_directory}
	{
		std::filesystem::create_directory(
			unit_directory + "/system/inittab"
		);
		std::filesystem::create_directory(
			unit_directory + "/system/rc"
		);

		for (const auto &[name, runlevel] : runlevels)
		{
			for (const auto &alias:runlevel.aliases)
			{
				runlevel_lookup.emplace(alias, name);
			}
		}
	}

	void start_rc(const std::string &identifier,
		      size_t linenum,
		      const std::string &comment,
		      std::set<std::string> &all_runlevels);

	//! Add an entry from the inittab file.
	void add_inittab(const inittab_entry &entry,
			 const std::string &comment)
	{
		add(entry, unit_directory + "/system/inittab/"
		    + entry.identifier, comment);
	}

	//! Add a unit for starting converted RC entries.

	void add_rc(const inittab_entry &entry)
	{
		add(entry, unit_directory + "/system/" + entry.identifier,
		    "");
	}

private:

	//! Keep track of all created unit files.
	std::unordered_set<std::string> all_units;

	void add(const inittab_entry &entry,
		 std::string filename,
		 const std::string &comment)
	{
		if (!all_units.insert(filename).second)
		{
			std::cerr << "Attempting to create "
				  << filename << " more than once."
				  << std::endl;
			error=true;
		}

		// Create the YAML file. Write it out only if its contents
		// different from the existing file. This avoids repeated
		// thrashing as vera attempts to reload everything, repeatedly.

		std::string new_contents;

		{
			std::ostringstream o;

			if (!comment.empty())
				o << "#\n# " << comment << "\n#\n\n";
			yaml_writer{o}.write(entry.create());

			new_contents=o.str();
		}

		std::ifstream i{filename};

		std::string current_contents{
			std::istreambuf_iterator<char>{i},
			std::istreambuf_iterator<char>{}
		};

		if (current_contents != new_contents)
		{
			std::ofstream o{filename + "~"};

			o << new_contents;

			o.close();

			if (o.fail())
			{
				std::cerr << "Cannot create "
					  << filename << std::endl;
				error=true;
			}

			std::error_code ec;

			std::filesystem::rename(filename + "~", filename, ec);

			if (ec)
			{
				std::cerr << "Cannot create " << filename
					  << ": " << ec.message() << std::endl;
				error=true;
			}
		}
	}
public:
	void cleanup();
};

//! When done, check for any existing files we did not see, they must be
//! obsolete.

void convert_inittab::cleanup()
{
	// Anything in system/inittab that was removed?

	for (auto b=std::filesystem::directory_iterator{
			unit_directory + "/system/inittab"
		}; b != std::filesystem::directory_iterator{}; ++b)
	{
		std::string s=b->path();

		if (all_units.count(s))
			continue;

		std::filesystem::remove(s);
	}

	// Any rc.* files that were no longer created?

	for (auto b=std::filesystem::directory_iterator{
			unit_directory + "/system/"
		}; b != std::filesystem::directory_iterator{}; ++b)
	{
		auto filename=b->path().filename().native();

		if (filename.substr(0, 3) != "rc.")
			continue;

		std::string s=b->path();

		if (all_units.count(s))
			continue;

		std::filesystem::remove(s);
	}
}

void convert_inittab::start_rc(const std::string &identifier,
			       size_t linenum,
			       const std::string &comment,
			       std::set<std::string> &all_runlevels)
{
	// Generate a "start" unit after this one, for
	// every runlevel that rc.K and rc.M is in, one
	// runlevel per start unit.
	//
	// Each unit manually runs the "vlad start" command
	// to start all the rc units.
	//
	// The dependencies of these units are that they
	// start after rc.K and stop before it.

	std::string saved_command=prev_command;
	std::vector<std::string> all_start_identifiers;

	for (auto &required_by : all_runlevels)
	{
		prev_command=saved_command;

		inittab_entry run_rc_start{
			prev_command,
			identifier + "-start-"
			+ required_by,
			""};

		run_rc_start.start_type="forking";
		run_rc_start.stop_type="target";

		all_start_identifiers.push_back(
			run_rc_start.identifier
		);
		run_rc_start.required_by_runlevel(required_by);

		run_rc_start.starting_command=
			"vlad start system/rc." + required_by;
		run_rc_start.stop_type="target";

		run_rc_start.description=identifier +
			": start rc.d scripts";
		run_rc_start.starts_before.push_back(
			"/system/rc");
		run_rc_start.stops_after.push_back(
			"/system/rc");

		if (!all_start_scripts.insert(
			    {
				    required_by, run_rc_start
			    }).second)
		{
			std::cerr << "Line "
				  << linenum
				  << ": duplicate rc script"
				  << "invocation detected"
				  << std::endl;
			error=true;
		}
	}

	// Generate a "started" unit, which starts after all
	// of the start units, and stops before them.
	//
	// In this manner, the RC units start/stop gets
	// book-ended after the corresponding rc.[MK] command.

	prev_command=saved_command;

	inittab_entry run_rc_started{
		prev_command,
		identifier + "-started",
		""};

	run_rc_started.stop_type="target";
	for (auto &required_by : all_runlevels)
	{
		run_rc_started.required_by_runlevel(
			required_by
		);
	}

	// The "started" unit starts after all system/rc
	// scripts and all the start scripts for this inittab
	// unit.
	run_rc_started.starts_after.push_back("/system/rc");
	run_rc_started.stops_before.push_back("/system/rc");

	for (auto &id : all_start_identifiers)
	{
		run_rc_started.starts_after.push_back(id);
		run_rc_started.stops_before.push_back(id);
	}
	run_rc_started.description=identifier +
		": started rc.d scripts";
	add_inittab(run_rc_started,
			      comment + " (rc started)");

	all_single_multi_runlevels.insert(
		all_runlevels.begin(),
		all_runlevels.end()
	);
}

#if 0
{
#endif
}

bool inittab(std::istream &etc_inittab,
	     const std::string &system_dir,
	     const runlevels &runlevels)
{
	convert_inittab generator{system_dir, runlevels};

	std::string s;
	size_t linenum=0;

	std::unordered_set<std::string> ondemand;

	while (std::getline(etc_inittab, s))
	{
		++linenum;

		// Strip comments, look for a non-blank line.
		s.erase(std::find(s.begin(), s.end(), '#'), s.end());

		auto colon=std::find(s.begin(), s.end(), ':');

		if (colon == s.end())
			continue;

		// Parse out the 1st and 2nd fields.

		std::string new_entry_identifier{s.begin(), colon};

		auto runlevels_iter=++colon;

		colon=std::find(colon, s.end(), ':');

		if (colon == s.end())
		{
			std::cerr << "Line " << linenum
				  << ": cannot find runlevel string."
				  << std::endl;
			generator.error=true;
			continue;
		}

		std::string runlevels{runlevels_iter, colon};

		auto actions_iter=++colon;

		colon=std::find(colon, s.end(), ':');

		if (colon == s.end())
		{
			std::cerr << "Line " << linenum
				  << ": cannot find actions string."
				  << std::endl;
			generator.error=true;
		}

		std::string actions{actions_iter, colon};

		if (!generator.ids_seen.insert(new_entry_identifier).second)
		{
			std::cerr << "Line " << linenum
				  << ": duplicate identifier \""
				  << new_entry_identifier << "\""
				  << std::endl;
			generator.error=true;
			continue;
		}

		if (actions == "off")
			continue;

		if (actions == "initdefault")
			continue;

		if (actions == "sysinit")
			continue;

		std::string starting_command{++colon, s.end()};

		std::set<std::string> required_by_runlevel;
		const char *start_type=nullptr;

		/*
		** The inittab's one-line character code gets mapped
		** into a list of long-name run levels, via the
		** runlevel_lookup mapping.
		*/

		std::set<std::string> all_runlevels;

		/*
		** Map certain actions to specific runlevels. The new unit
		** declares that it's required-by these runlevels.
		*/

		if (actions == "ctrlaltdel")
		{
			required_by_runlevel.insert(SIGINT_UNIT);
		}
		else if (actions == "powerfail")
		{
			required_by_runlevel.insert(PWRFAIL_UNIT);
		}
		else if (actions == "powerwait")
		{
			start_type="forking";
			required_by_runlevel.insert(PWRFAIL_UNIT);
		}
		else if (actions == "powerok")
		{
			required_by_runlevel.insert(PWROK_UNIT);
		}
		else if (actions == "powerokwait")
		{
			start_type="forking";
			required_by_runlevel.insert(PWROK_UNIT);
		}
		else if (actions == "powerfailnow")
		{
			required_by_runlevel.insert(PWRFAILNOW_UNIT);
		}
		else if (actions == "powerfailnowwait")
		{
			start_type="forking";
			required_by_runlevel.insert(PWRFAILNOW_UNIT);
		}
		else if (actions == "kbrequest")
		{
			required_by_runlevel.insert(SIGWINCH_UNIT);
		}
		else if (actions == "boot")
		{
			required_by_runlevel.insert("boot");
		}
		else if (actions == "bootwait")

		{
			required_by_runlevel.insert("boot");
			start_type="forking";
		}
		else
		{
			// And for other actions we look at the actual
			// runlevel declarations, with the action controlling

			if (actions == "respawn")
			{
				start_type="respawn";
			}
			else if (actions == "wait")
			{
				start_type="forking";
			}
			else if (actions == "once")
			{
			}
			else if (actions == "ondemand")
			{
				// The loop below will put entry into the
				// ondemand runlevel.
			}
			else if (actions == "ondemandwait")
			{
				// The loop below will put entry into the
				// ondemand runlevel.
				start_type="forking";
			}

			for (auto c:runlevels)
			{
				switch (c) {
				case 'a':
				case 'A':
					generator.all_single_multi_runlevels
						.insert("a");
					all_runlevels.insert("a");
					continue;
				case 'b':
				case 'B':
					generator.all_single_multi_runlevels
						.insert("b");
					all_runlevels.insert("b");
					continue;
				case 'c':
				case 'C':
					generator.all_single_multi_runlevels
						.insert("c");
					all_runlevels.insert("c");
					continue;
				}
				auto iter=generator.runlevel_lookup.find(
					std::string{
						&c, &c+1
					});

				if (iter == generator.runlevel_lookup.end())
				{
					std::cerr << "Line " << linenum
						  << ": unknown runlevel "
						  << c << std::endl;
					generator.error=true;
				}
				else
				{
					all_runlevels.insert(
						iter->second);
				}
			}
			for (auto &rl:all_runlevels)
				required_by_runlevel.insert(rl);
		}

		// rc.0 and rc.6 run the initscripts first, before getting
		// down to business.
		if (starting_command == "/etc/rc.d/rc.0" ||
		    starting_command == "/etc/rc.d/rc.6")
		{
			generator.start_rc(new_entry_identifier,
					   linenum,
					   s,
					   all_runlevels);
		}
		inittab_entry new_entry{
			generator.prev_command,
			std::move(new_entry_identifier),
			starting_command
		};

		if (start_type)
			new_entry.start_type=start_type;
		for (auto &rl : required_by_runlevel)
			new_entry.required_by_runlevel(rl);

		// rc.K and rc.M run initscripts after it finished its
		// business.

		bool is_sysvinit_after =
			new_entry.starting_command == "/etc/rc.d/rc.K"
			|| new_entry.starting_command == "/etc/rc.d/rc.M";

		// Intercept rc.0 and rc.6

		if (new_entry.starting_command == "/etc/rc.d/rc.0")
			new_entry.starting_command = "vlad sysdown 0 "
				+ new_entry.starting_command;
		else if (new_entry.starting_command == "/etc/rc.d/rc.6")
			new_entry.starting_command = "vlad sysdown 6 "
				+ new_entry.starting_command;

		generator.add_inittab(new_entry, s);

		if (is_sysvinit_after)
		{
			generator.start_rc(new_entry.identifier,
					   linenum,
					   s,
					   all_runlevels);
		}
	}

	// Now, take each <inittab>-start script that includes the
	// "vlad start" command for its init runlevel, and add a "vlad stop"
	// command for all others.

	for (auto &[runlevel, unit]:generator.all_start_scripts)
	{
		for (auto &[other_runlevel, other_unit]:generator.all_start_scripts)
		{
			if (runlevel == other_runlevel)
				continue;

			unit.starting_command += " &&\\\n    vlad stop system/rc." +
				other_runlevel;
		}

		generator.add_inittab(unit, "");
	}

	// And now generate the rc.<target> targets

	for (auto &rc_runlevel : generator.all_single_multi_runlevels)
	{
		generator.prev_command = "";

		inittab_entry run_rc{
			generator.prev_command,
			"rc." + rc_runlevel,
			""};
		run_rc.description="initscripts in system/rc that are required-by: /system/rc." + rc_runlevel;
		run_rc.stop_type="target";

		generator.add_rc(run_rc);
	}

	if (!generator.error)
		generator.cleanup();

	if (generator.error)
		return false;
	return true;
}