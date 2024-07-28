/*
** Copyright 2024 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "inittab.H"
#include "log.H"
#include "hook.H"
#include "verac.h"
#include "yaml_writer.H"
#include "proc_loader.H"
#include <algorithm>
#include <set>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <string_view>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <sys/stat.h>

namespace {
#if 0
}
#endif

/*! The previous identifier for each runlevel

  As each line in /etc/inittab gets converted, we keep track of the
  previous processed identifier for each runlevel.
*/

typedef std::unordered_map<std::string, std::string> prev_commands_t;

/*! All runlevels for the current command being processd
 */

typedef std::set<std::string> all_runlevels_t;

/*!
  A new inittab entry being imported.
*/

struct inittab_entry {

	/*!
	  Previous inittab commands
	*/
	prev_commands_t &prev_commands;

	/*!
	  This entry's run levels.

	*/
	const all_runlevels_t all_runlevels;

	/*! First field in the inittab line
	 */
	std::string identifier;

	/*! The last file in the inittab line

	  It becomes the starting command.
	*/

	std::string starting_command;

	/*! Non-defautl starting timeout */
	std::string starting_timeout;

	/*! Stopping command, if we know it */
	std::string stopping_command;

	/*! Restarting command, if we know it */
	std::string restarting_command;

	/*! Reload command, if we know it */
	std::string reloading_command;

	/*! The description that goes into the generated unit file */
	std::string description;

	/*! The unit's alternative-group */

	std::string alternative_group;

	/*! Extra headers.

	  This is used to link an inittab entry to an rc.d script

	 */

	std::unordered_map<std::string, std::string> x;
	/* Unit's starting type */
	const char *start_type="oneshot";

	/* Unit's stop type */
	const char *stop_type="manual";

	// Unit or units where this inittab/namespace entry starts in.
	std::vector<std::string> required_by;

	// Unit or units where this inittab/namespace entry requires
	std::vector<std::string> this_requires;

	// Additional dependencies
	std::vector<std::string> starts_before, starts_after,
		stops_before, stops_after;

	//! This unit starts in this runlevel.

	void required_by_runlevel(const std::string &runlevel)
	{
		required_by.push_back("/system/"+runlevel);
	}

	//! New entry read from /etc/inittab

	inittab_entry(prev_commands_t &prev_commands,
		      const all_runlevels_t &all_runlevels,
		      std::string identifier,
		      std::string starting_command,
		      std::string description)
		: prev_commands{prev_commands},
		  all_runlevels{all_runlevels},
		  identifier{std::move(identifier)},
		  starting_command{std::move(starting_command)},
		  description{std::move(description)}
	{
		// If there's a previous command this one will start after it
		// and stop before it.

		std::unordered_set<std::string> all_prev_commands;

		for (auto &rl:all_runlevels)
		{
			auto &prev_command=prev_commands[rl];

			if (prev_command.empty())
				continue;

			if (!all_prev_commands.insert(prev_command).second)
				continue;

			starts_after.push_back(prev_command);
			stops_before.push_back(prev_command);
		}
	}

	//! Additional entries generated.

	inittab_entry(inittab_entry &prev_entry,
		      std::string identifier,
		      std::string starting_command,
		      std::string description)
		: inittab_entry{
				prev_entry.prev_commands,
				prev_entry.all_runlevels,
				std::move(identifier),
				std::move(starting_command),
				std::move(description)
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

	if (!alternative_group.empty())
		unit.emplace_back(
			std::make_shared<yaml_write_scalar>(
				"alternative-group"
			),
			std::make_shared<yaml_write_scalar>(
				alternative_group
			)
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
	if (!this_requires.empty())
	{
		unit.emplace_back(
			std::make_shared<yaml_write_scalar>("requires"),
			std::make_shared<yaml_write_seq>(
				this_requires.begin(), this_requires.end()
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

	if (!starting_timeout.empty())
	{
		starting.emplace_back(
			std::make_shared<yaml_write_scalar>("timeout"),
			std::make_shared<yaml_write_scalar>(starting_timeout)
		);
	}

	if (!stopping_command.empty())
		stopping.emplace_back(
			std::make_shared<yaml_write_scalar>("command"),
			std::make_shared<yaml_write_scalar>(stopping_command)
		);

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

	if (!restarting_command.empty())
	{
		unit.emplace_back(
			std::make_shared<yaml_write_scalar>("restart"),
			std::make_shared<yaml_write_scalar>(restarting_command)
		);
	}

	if (!reloading_command.empty())
	{
		unit.emplace_back(
			std::make_shared<yaml_write_scalar>("reload"),
			std::make_shared<yaml_write_scalar>(reloading_command)
		);
	}

	for (auto &rl:all_runlevels)
		prev_commands[rl]=identifier;

	for (auto &[name, value] : x)
	{
		unit.emplace_back(
			std::make_shared<yaml_write_scalar>(name),
			std::make_shared<yaml_write_scalar>(value)
		);
	}

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

	std::unordered_map<std::string, std::string> prev_commands;

	// Keep track of runlevels that run rc.? scripts
	std::set<std::string> all_single_multi_runlevels;

	convert_inittab(const std::string &unit_directory,
			const runlevels &runlevels)
		: unit_directory{unit_directory}
	{
		// Converted entries in /etc/inittab go here
		std::filesystem::create_directory(
			unit_directory + "/system/inittab"
		);

		// Converted entries from /etc/rc.d/rc?.d go here
		std::filesystem::create_directory(
			unit_directory + "/system/rc"
		);

		// Scripts sniffed out of /etc/rc.d/rc.M go here
		std::filesystem::create_directory(
			unit_directory + "/system/rc.M"
		);

		// Create mapping from legacy init runlevels to descriptive
		// runlevel names.

		for (const auto &[name, runlevel] : runlevels)
		{
			for (const auto &alias:runlevel.aliases)
			{
				runlevel_lookup.emplace(alias, name);
			}
		}
	}

	//! Generate targets for starting converted /etc/rc.d units

	void start_rc(
		//! Identifier from /etc/inittab, the first field
		const std::string &identifier,

		//! /etc/inittab line number, for error messages
		size_t linenum,

		//! What to write into the unit specification file.
		const std::string &comment,

		//! Which runlevels the /etc/rc.d entries are started for
		all_runlevels_t &all_runlevels,

		//! Additional command to run from the "started" unit

		const char *extra_stopping_command_for_started);

	//! Generate target for running /etc/rc.d/rc.local

	void start_local(const std::string &identifier,
			 size_t linenum,
			 const std::string &comment,
			 all_runlevels_t &all_runlevels);

	//! Add an entry from the inittab file.
	void add_inittab(const inittab_entry &entry,
			 const std::string &comment)
	{
		add(entry, unit_directory + "/system/inittab/"
		    + entry.identifier, comment);
	}

	/*! Add a unit for starting converted RC entries.

	** A unit for each runlevel that converted /etc/rc.d units, for that
	** run levels, are required-by.
	*/

	void add_rc(const inittab_entry &entry)
	{
		add(entry, unit_directory + "/system/" + entry.identifier,
		    "");
	}

	/*! Add a unit for starting entries executed by rc.M

	** A unit for each script that /etc/rc.d/rc.M runs, if it's
	** executable.
	*/

	void add_rcm(const inittab_entry &entry)
	{
		add(entry, unit_directory + "/system/rc.M/" + entry.identifier,
		    "start /etc/rc.d/" + entry.identifier
		    + " from /etc/rc.d/rc.M");
	}

	/*! Add a unit for starting /etc/rc.d/rc?.d entries */

	void add_rcd(const inittab_entry &entry)
	{
		add(entry, unit_directory + "/system/rc/" + entry.identifier,
		    "start " + entry.identifier);
	}

	/*! Add an ad-hoc system unit */

	void add_adhoc_system(
		const inittab_entry &entry,
		const std::string &comment
	)
	{
		add(entry, unit_directory + "/system/" + entry.identifier,
		    comment);
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

		if (filename == "rc.M")
			continue; // This is a directory, see below.

		if (filename.substr(0, 3) != "rc.")
			continue;

		std::string s=b->path();

		if (all_units.count(s))
			continue;

		std::filesystem::remove(s);
	}

	// Anything in system/rc that was removed?

	for (auto b=std::filesystem::directory_iterator{
			unit_directory + "/system/rc/"
		}; b != std::filesystem::directory_iterator{}; ++b)
	{
		std::string s=b->path();

		if (all_units.count(s))
			continue;

		std::filesystem::remove(s);
	}

	// Anything in system/rc.M that was removed?

	for (auto b=std::filesystem::directory_iterator{
			unit_directory + "/system/rc.M/"
		}; b != std::filesystem::directory_iterator{}; ++b)
	{
		std::string s=b->path();

		if (all_units.count(s))
			continue;

		std::filesystem::remove(s);
	}
}

void convert_inittab::start_rc(const std::string &identifier,
			       size_t linenum,
			       const std::string &comment,
			       all_runlevels_t &all_runlevels,
			       const char *extra_stopping_command_for_started)
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

	for (auto &required_by : all_runlevels)
	{
		all_runlevels_t just_one;

		just_one.insert(required_by);

		inittab_entry run_rc_start{
			prev_commands,
			just_one,
			identifier + "-start-" + required_by,
			"",
			identifier + ": start rc.d scripts"};

		run_rc_start.start_type="forking";
		run_rc_start.stop_type="target";

		run_rc_start.required_by_runlevel(required_by);

		run_rc_start.starting_command=
			"vlad --nowait start system/rc." + required_by;
		run_rc_start.stop_type="target";

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

		// Generate a "started" unit, which starts after all
		// of the start units, and stops before them.
		//
		// In this manner, the RC units start/stop gets
		// book-ended after the corresponding rc.[MK] command.

		inittab_entry run_rc_started{
			prev_commands,
			just_one,
			identifier + "-started-" + required_by,
			"",
			identifier + ": started rc.d scripts"};

		run_rc_started.stop_type="target";

		run_rc_started.required_by_runlevel(
			required_by
		);

		// The "started" unit starts after all system/rc
		// scripts and all the start scripts for this inittab
		// unit.
		run_rc_started.starts_after.push_back("/system/rc");
		run_rc_started.stops_before.push_back("/system/rc");

		run_rc_started.description=identifier +
			": started rc.d scripts";
		run_rc_started.stopping_command=
			extra_stopping_command_for_started;
		add_inittab(run_rc_started,
			    comment + " (rc started)");
	}

	all_single_multi_runlevels.insert(
		all_runlevels.begin(),
		all_runlevels.end()
	);
}

void convert_inittab::start_local(const std::string &identifier,
				  size_t linenum,
				  const std::string &comment,
				  all_runlevels_t &all_runlevels)
{
	inittab_entry run_rc_local{
		prev_commands,
		all_runlevels,
		identifier + "-run-local",
		"test ! -x /etc/rc.d/rc.local.init ||"
		" /etc/rc.d/rc.local.init start",
		identifier + ": started rc.local"
	};

	run_rc_local.stopping_command=
		"test ! -x /etc/rc.d/rc.local_shutdown.init ||"
		" /etc/rc.d/rc.local_shutdown.init stop";

	run_rc_local.start_type="forking";
	run_rc_local.stop_type="manual";

	for (auto &required_by:all_runlevels)
		run_rc_local.required_by_runlevel(required_by);

	run_rc_local.description=identifier +
		": started rc.local";
	add_inittab(run_rc_local,
		    comment + " (rc.local started)");
}

/*
  Metadata while parsing /etc/inittab lines.

  The physical structure of /etc/inittab is parsed in C, which invokes
  a callback with a passthrough void pointer that points to this object.

  The C code trampoline invokes the closure that does the real work, and
  traps C++ exceptions, which set the error flag.

 */

struct parse_inittab_data {
	std::function<void (const char *,
			    const char *,
			    const char *, const char *,
			    const char *)> *cbptr;
	bool error;
};

#if 0
{
#endif
}

extern "C" {

	/*
	** Callback from C parser.
	*/

	static void parse_inittab_trampoline(
		const char *orig_line,
		const char *identifier,
		const char *runlevels,
		const char *action,
		const char *command,
		void *cbarg)
	{
		auto *d=reinterpret_cast<parse_inittab_data *>(cbarg);

		try {
			(*d->cbptr)(orig_line,
				    identifier,
				    runlevels,
				    action,
				    command);
		} catch (const std::exception &e)
		{
			std::cerr << e.what() << std::endl;
			d->error=true;
		}
	}
}

bool parse_inittab(const std::string &filename,
		   std::function<void (const char *,
				       const char *,
				       const char *, const char *,
				       const char *)> parser)
{
	parse_inittab_data d{&parser, false};

	FILE *fp=fopen(filename.c_str(), "r");

	if (!fp)
	{
		std::cerr << filename << ": " << strerror(errno) << std::endl;
		return false;
	}

	parse_inittab(fp, parse_inittab_trampoline, &d);

	fclose(fp);
	return (!d.error);
}

namespace {
#if 0
}
#endif

struct inittab_converter {

	const std::string &unit_dir;
	const std::string &pkgdata_dir;
	const runlevels &runlevels_config;
	std::string rcdir;
	std::string &initdefault;

	convert_inittab generator{unit_dir, runlevels_config};

	std::string s;
	size_t linenum=0;

	std::unordered_set<std::string> ondemand;

	inittab_converter(const std::string &unit_dir,
			  const std::string &pkgdata_dir,
			  const runlevels &runlevels_config,
			  std::string rcdir,
			  std::string &initdefault)
		: unit_dir{unit_dir},
		  pkgdata_dir{pkgdata_dir},
		  runlevels_config{runlevels_config},
		  rcdir{std::move(rcdir)},
		  initdefault{initdefault},
		  generator{unit_dir, runlevels_config}
	{
	}

	void operator()(const char *s,
			std::string new_entry_identifier,
			std::string runlevels,
			std::string actions,
			std::string starting_command)
	{
		if (!generator.ids_seen.insert(new_entry_identifier).second)
		{
			std::cerr << "Line " << linenum
				  << ": duplicate identifier \""
				  << new_entry_identifier << "\""
				  << std::endl;
			generator.error=true;
			return;
		}

		if (actions == "off")
			return;

		if (actions == "initdefault")
		{
			initdefault=runlevels;
			return;
		}

		if (actions == "sysinit")
			return;

		std::set<std::string> required_by_runlevel;
		const char *start_type=nullptr;

		/*
		** The inittab's one-line character code gets mapped
		** into a list of long-name run levels, via the
		** runlevel_lookup mapping.
		*/

		all_runlevels_t all_runlevels;

		bool ondemand=false;
		bool inrunlevel=false;

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
					ondemand=true;
					continue;
				case 'b':
				case 'B':
					generator.all_single_multi_runlevels
						.insert("b");
					all_runlevels.insert("b");
					ondemand=true;
					continue;
				case 'c':
				case 'C':
					generator.all_single_multi_runlevels
						.insert("c");
					all_runlevels.insert("c");
					ondemand=true;
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
					inrunlevel=true;
					all_runlevels.insert(
						iter->second);
				}
			}
			if (ondemand && inrunlevel)
			{
				std::cerr << "Line " << linenum
					  << ": specifies both an on-demand "
					"and a system runlevel, "
					"simultaneously" << std::endl;
				generator.error=true;
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
					   all_runlevels, "");
		}

		std::string description=new_entry_identifier+": "
			+ starting_command;
		inittab_entry new_entry{
			generator.prev_commands,
			all_runlevels,
			std::move(new_entry_identifier),
			starting_command,
			std::move(description)
		};

		if (start_type)
			new_entry.start_type=start_type;
		for (auto &rl : required_by_runlevel)
			new_entry.required_by_runlevel(rl);

		if (inrunlevel)
		{
			new_entry.this_requires.push_back("../boot");
		}
		bool is_local_after=
			new_entry.starting_command == "/etc/rc.d/rc.M";

		if (is_local_after)
		{
			new_entry.starting_timeout="300";

			// We're going to initiate stopping of all containers
			// for the extracted /etc/rc.d/rc.M-started
			// units. They all go into system/rc.M. We want to
			// wait running rc.K until they're stopped, so this
			// stops_after all rc.M scripts
			new_entry.stops_after.push_back("../rc.M");

			// Use vera-rcb to replace all invocations of
			// /etc/rc.d/rc.script with the equivalent
			// "vlad start" command for the units in unit_dir/rc.M
			new_entry.starting_command =
				pkgdata_dir + "/vera-rcm "
				+ unit_dir + " /etc/rc.d/rc.M | /bin/bash";

			// Run rc.K to stop rc.M. Makes sense.
			new_entry.stopping_command =
				pkgdata_dir + "/vera-rck "
				"/etc/rc.d/rc.K | /bin/bash";
		}
		// rc.K and rc.M run initscripts after it finished its
		// business.

		bool is_sysvinit_after=is_local_after ||
			new_entry.starting_command == "/etc/rc.d/rc.K";

		// Intercept rc.0, rc.6, and rc.K

		if (new_entry.starting_command == "/etc/rc.d/rc.0")
			new_entry.starting_command = "vlad sysdown 0 "
				+ new_entry.starting_command;
		else if (new_entry.starting_command == "/etc/rc.d/rc.6")
			new_entry.starting_command = "vlad sysdown 6 "
				+ new_entry.starting_command;
		else if (new_entry.starting_command == "/etc/rc.d/rc.K")
			new_entry.starting_command =
				pkgdata_dir + "/vera-rck "
				+ new_entry.starting_command + " | /bin/bash";

		generator.add_inittab(new_entry, s);

		if (is_sysvinit_after)
		{
			// rc.M runs rc.sysvinit. And we want to
			// stop the units started from rc.M before we
			// stop rc.M. All those units require rc.M.target, so
			// we just do a stop of system/rc.M.target
			generator.start_rc(
				new_entry.identifier,
				linenum,
				s,
				all_runlevels,
				is_local_after ?
				"vlad --nowait stop system/rc.M.target":""
			);
		}

		if (is_local_after)
		{
			generator.start_local(new_entry.identifier,
					      linenum,
					      s,
					      all_runlevels);
		}
	}

	bool parse_rc_m();
private:

	//! What parse_rc_m_inet2 is getting called for
	enum class etc_rc {
		rc_M,		//!< \c /etc/rc.d/rc.M
		rc_inet2	//!< \c /etc/rc.d/rc.inet2
	};

	void parse_rc_m_inet2(
		std::istream &script,
		etc_rc script_id,
		std::string &last_rc_M_or_inet2,
		inittab_entry &rc_M_target
	);

public:
	bool finish();
};

/*! Parse /etc/rc.d/rc.M

Look for -x /etc/rc.d/rc.script, followed by "/etc/rc.d/rc.script start".

Each found rc.script gets converted into system/rc.M/rc.script unit. All of
these units are required-by system/rc.M.target, so it gets started automatically
and we'll do a stop of system/rc.M.target to stop all rc.script units.

*/

bool inittab_converter::parse_rc_m()
{
	std::ifstream rc_m{rcdir + "/rc.M"};

	if (!rc_m.is_open())
		return true;

	prev_commands_t rc_M_target_dummy;
	all_runlevels_t rc_M_target_none;

	// Last converted rc.M-started script
	std::string last_rc_M_or_inet2;

	inittab_entry rc_M_target{
		rc_M_target_dummy,
		rc_M_target_none,
		"rc.M.target",
		"",
		"stop services started from rc.M"
	};

	rc_M_target.description =
		"Dummy target that all rc.M/ units depend on";

	parse_rc_m_inet2(
		rc_m,
		etc_rc::rc_M,
		last_rc_M_or_inet2,
		rc_M_target
	);


	std::sort(rc_M_target.required_by.begin(),
		  rc_M_target.required_by.end());
	generator.add_rc(rc_M_target);
	return true;
}

void inittab_converter::parse_rc_m_inet2(
	std::istream &script,
	etc_rc script_id,
	std::string &last_rc_M_or_inet2,
	inittab_entry &rc_M_target
)
{
	std::string line;

	// "-x <string>" found in /etc/rc.d/rc.M. If we then find a
	// "<string> start" we make a note of it.

	std::unordered_set<std::string> checkx;

	while (std::getline(script, line))
	{
		// Basic parsing into whitespace-delimited words
		std::vector<std::string> words;

		for (auto b=line.begin(), e=line.end(); b != e;)
		{
			if (*b == ' ')
			{
				++b;
				continue;
			}

			auto p=b;

			while (b != e && *b != ' ')
			{
				++b;
			}
			words.emplace_back(p, b);
		}

		/*
if [ -x /etc/rc.d/rc.inet2 ]; then
  /etc/rc.d/rc.inet2
fi

		*/

		if (words.size() == 1 && script_id == etc_rc::rc_M &&
		    words[0] == rcdir + "/rc.inet2" &&
		    checkx.find(words[0]) != checkx.end())
		{
			std::ifstream rc_inet2{rcdir + "/rc.inet2"};

			if (rc_inet2.is_open())
			{
				parse_rc_m_inet2(
					rc_inet2,
					etc_rc::rc_inet2,
					last_rc_M_or_inet2,
					rc_M_target
				);
			}
		}

		// Now inspect each word.

		for (auto b=words.begin(), e=words.end(); b != e;)
		{
			if (*b != "-x")
			{
				++b;

				if (b == e || *b != "start")
					continue;

				// We have "something start".

				// Does this "something" start with /etc/rc.d/?

				if (b[-1].substr(0, 10) != "/etc/rc.d/")
					continue;

				// And we've seen a -x for it?

				if (checkx.find(b[-1]) == checkx.end())
					continue;

				std::string script=b[-1].substr(10);

				// Both -x /etc/rc.d/script and
				// /etc/rc.d/script start were found

				// We'll manually take care of stopping after.

				prev_commands_t dummy;
				all_runlevels_t none;

				std::string unit_name=script;

				// ip_forward => ip-forward
				std::replace(
					unit_name.begin(),
					unit_name.end(),
					'_',
					'-');

				std::string launch="/etc/rc.d/" + script;

				// See if there's a restart or a reload command

				std::ifstream i{rcdir + "/" + script};
				std::string line;

				bool has_restart=false;
				bool has_reload=false;

				while (std::getline(i, line))
				{
					auto e=line.end();
					auto b=std::find_if(
						line.begin(),
						e,
						[]
						(char c)
						{
							return c != ' ' &&
								c != '\t' &&
								c != '\'' &&
								c != '"';
						}
					);

					if (b == e)
						continue;

					auto p=std::find_if(
						b,
						e,
						[]
						(char c)
						{
							return c == '\'' ||
								c == '"' ||
								c == ')';
						}
					);

					std::string_view word{b, p};

					if (word == "restart")
					{
						has_restart=true;
					}

					if (word == "reload")
					{
						has_reload=true;
					}
				}

				inittab_entry run_rc{
					dummy,
					none,
					unit_name,
					launch + " start",
					launch
				};
				run_rc.x.emplace(x_chmod_script_header, launch);

				if (b-words.begin() >= 2 &&
				    b[-2] == "sh")
				{
					launch = "sh " + launch;
				}

				run_rc.stopping_command=
					launch + " stop";

				if (has_restart)
				{
					run_rc.restarting_command=
						launch + " restart";
				}

				if (has_reload)
				{
					run_rc.reloading_command=
						launch + " reload";
				}

				run_rc.start_type="forking";
				if (!last_rc_M_or_inet2.empty())
					run_rc.stops_before.push_back(
						last_rc_M_or_inet2
					);
				last_rc_M_or_inet2=unit_name;

				rc_M_target.required_by.push_back(
					"rc.M/" + unit_name);

				if (script_id == etc_rc::rc_inet2)
				{
					run_rc.this_requires.push_back(
						"../network-online"
					);
				}

				generator.add_rcm(run_rc);

				continue;
			}

			// Look at the next word after -x

			if (++b != e && b->substr(0, 10) == "/etc/rc.d/")
			{
				/*

				  /etc/rc.d/rc.M starts syslogd, but then
				  there's also another attempt in
				  /etc/rc.d/rc.inet2, let's ignore that one
				*/

				if (script_id == etc_rc::rc_inet2 &&
				    *b == rcdir + "/rc.syslog")
					continue;

				checkx.insert(*b);
			}
		}
	}
}

bool inittab_converter::finish()
{
	// Add all start scripts

	for (auto &[runlevel, unit]:generator.all_start_scripts)
	{
		generator.add_inittab(unit, "");
	}

	// And now generate the rc.<target> targets

	for (auto &rc_runlevel : generator.all_single_multi_runlevels)
	{
		all_runlevels_t none;

		inittab_entry run_rc{
			generator.prev_commands,
			none,
			"rc." + rc_runlevel,
			"",
			"initscripts in system/rc that are required-by:"
			" /system/rc." + rc_runlevel
		};
		run_rc.alternative_group="rc";
		run_rc.stop_type="target";

		generator.add_rc(run_rc);
	}

	/////////////////////////////////////////////////////////////////////
	//
	// Attempt to parse /etc/rc.d/rc?.d directories

	std::set<std::string> sorted_runlevels; // Deterministic order

	for (const auto &[name, rl] : runlevels_config)
		sorted_runlevels.insert(name);

	// First, we track each S script by its device and inode, and
	// we store the following information about each one:

	struct rc_script_info {

		// Full path to the first "S" instance of the script
		std::filesystem::path path;

		// All runlevels it's found in.
		all_runlevels_t runlevels;

		// Full path to the first "K" instance of the script
		std::filesystem::path shutdown_path;

		bool operator<(const rc_script_info &o) const
		{
			return path < o.path;
		}
	};

	// Look up a script by its inode.
	std::map<std::tuple<dev_t,ino_t>, rc_script_info> file_lookup;

	// Map a filename to its inode, detects same scripts in different
	// rc directories which are different.

	std::map<std::string, std::tuple<dev_t, ino_t>> ino_lookup;

	// While we're scanning the rc?.d directory we also keep track of which
	// K files were found.

	std::map<std::tuple<dev_t, ino_t>, std::filesystem::path> klookup;

	for (const auto &name:sorted_runlevels)
	{
		auto &rl=runlevels_config.find(name)->second;

		std::set<std::string> sorted_aliases{
			rl.aliases.begin(), rl.aliases.end()
		};

		for (auto &alias:sorted_aliases)
		{
			if (alias.size() != 1)
				continue;

			std::string rcdir_name=rcdir + "/rc"+alias+".d";

			std::error_code ec;

			std::filesystem::directory_entry de{rcdir_name, ec};

			if (ec)
				continue;

			for (auto b=std::filesystem::directory_iterator{
					rcdir_name
				}; b != std::filesystem::directory_iterator{};
			     ++b)
			{
				auto path=b->path();

				std::string s=path;
				std::string f=path.filename();

				if (f.find_first_of(" \r\t\n~#")
				    != std::string::npos)
					continue;

				struct stat stat_buf;

				if (stat(s.c_str(), &stat_buf))
					continue;

				std::tuple dev_ino{
					stat_buf.st_dev,
					stat_buf.st_ino
				};

				if (*f.c_str() == 'S')
				{
					auto ino_lookup_iter=
						ino_lookup.emplace(
							f,
							dev_ino
						);

					if (ino_lookup_iter.first->second !=
					    dev_ino)
					{
						throw std::runtime_error{
							"Inconsistent names: "
								+ s
								+ " does not "
								"match another "
								+ f
								};
					}

					auto file_lookup_iter=
						file_lookup.emplace(
							dev_ino,
							rc_script_info{
								path
							});

					auto &existing_path=
						file_lookup_iter.first->second
						.path;

					// This'll also pick up the same
					// file in the same directory...

					if (existing_path.filename() !=
					    path.filename())
						throw std::runtime_error{
							"Inconsistent names: "
								+ s
								+ " and "
								+ static_cast<
								std::string
								>(existing_path
								)};
					file_lookup_iter.first->second.runlevels
						.insert(name);
				}
				if (*f.c_str() == 'K')
				{
					klookup.emplace(dev_ino, path);
				}
			}
		}
	}

	// We can now compile a sorted map of rc files' start links, to their
	// stop links, if there are any.

	std::set<rc_script_info> rc_files;

	for (auto &[devino, info] : file_lookup)
	{
		auto kiter=klookup.find(devino);

		if (kiter != klookup.end())
			info.shutdown_path=kiter->second;

		rc_files.insert(std::move(info));
	}


	generator.prev_commands.clear();

	for (auto f:rc_files)
	{
		std::string extension=f.path.extension();

		std::string filename=f.path.filename();

		// If the script is a symlink replace it with the symlink
		// target.
		{
			std::error_code ec;

			std::filesystem::directory_entry de{f.path, ec};

			if (!ec && de.is_symlink(ec))
			{
				auto link=std::filesystem::read_symlink(
					f.path, ec);

				if (!ec)
				{
					if (link.is_absolute())
						f.path=link;
					else
						f.path.replace_filename(link);
				}
			}

			f.path=f.path.lexically_normal();
		}

		inittab_entry new_entry{
			generator.prev_commands,
			f.runlevels,
			filename,
			static_cast<std::string>(f.path),
			static_cast<std::string>(f.path)
		};

		new_entry.starting_command=
			"test ! -x " + static_cast<std::string>(f.path) + " || "
			+ (extension == ".sh" ? "sh ":"")
			+ static_cast<std::string>(f.path) + " start";

		if (!f.shutdown_path.empty())
		{
			std::error_code ec;

			extension=f.shutdown_path.extension();

			std::filesystem::directory_entry
				de{f.shutdown_path, ec};

			if (!ec && de.is_symlink(ec))
			{
				auto link=std::filesystem::read_symlink(
					f.shutdown_path, ec);

				if (!ec)
				{
					if (link.is_absolute())
						f.shutdown_path=link;
					else
						f.shutdown_path
							.replace_filename(link);
				}
			}

			f.shutdown_path=f.shutdown_path.lexically_normal();

			new_entry.stopping_command=
				"test ! -x " + static_cast<std::string>(
					f.shutdown_path
				) + " || "
				+ (extension == ".sh" ? "sh ":"")
				+ static_cast<std::string>(f.shutdown_path)
				+ " stop";
		}
		new_entry.start_type="forking";
		new_entry.stop_type="manual";

		for (auto &l:f.runlevels)
			new_entry.required_by.push_back("/system/rc."+l);

		generator.add_rcd(new_entry);
	}
	if (!generator.error)
		generator.cleanup();

	if (generator.error)
		return false;
	return true;
}

#if 0
{
	#endif
}

bool inittab(std::string filename,
	     std::string rcdir,
	     const std::string &unit_dir,
	     const std::string &pkgdata_dir,
	     const runlevels &runlevels,
	     std::string &initdefault)
{
	bool error=false;

	inittab_converter converter{unit_dir, pkgdata_dir, runlevels,
		std::move(rcdir),
		initdefault};

	{
		prev_commands_t stub1;
		all_runlevels_t stub2;

		inittab_entry wait_online{
			stub1, stub2,
			"network-online",
			"",
			"network online"
		};

		wait_online.stop_type="target";

		converter.generator.add_adhoc_system(
			wait_online,
			"required by all rc.inet2-started scripts"
		);
	}

	return parse_inittab(
		filename.c_str(),
		[&]
		(const char *orig_line,
		 const char *identifier,
		 const char *runlevels,
		 const char *action,
		 const char *processed_command)
		{
			++converter.linenum;

			if (!identifier)
				return;

			if (!runlevels)
			{
				std::cerr << "Line " << converter.linenum
					  << ": cannot find runlevel string."
					  << std::endl;
				error=true;
				return;
			}

			if (!action)
			{
				std::cerr << "Line " << converter.linenum
					  << ": cannot find actions string."
					  << std::endl;
				error=true;
				return;
			}
			converter(orig_line,
				  identifier,
				  runlevels,
				  action,
				  processed_command);
		}
	) && !error && converter.parse_rc_m() && converter.finish();
}
