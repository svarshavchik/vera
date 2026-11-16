/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef parsed_yaml_container_H
#define parsed_yaml_container_H

#include "config.h"
#include "parsed_yaml.H"
#include "proc_container.H"
#include "proc_loaderfwd.H"
#include "proc_new_container.H"
#include <yaml.h>
#include <functional>
#include <filesystem>
#include <unordered_set>
#include <string>

/*! Container-specific YAML parsing extensions.

  Extends parsed_yaml with methods for loading container configuration
  from YAML documents. These methods are specific to container parsing
  and are separated from the base parsed_yaml class for better
  organization and reduced coupling.

 */
struct parsed_yaml_container : public parsed_yaml {

	using parsed_yaml::parsed_yaml;

	/*! Parse starting/stopping section in a unit specification.

	  @param n the YAML node to parse
	  @param name the unit name
	  @param error error callback function
	  @param hier_name hierarchical path name
	  @param command output: parsed command string
	  @param timeout output: parsed timeout value
	  @param before output: before dependencies set
	  @param after output: after dependencies set
	  @param new_container output: the new container object
	  @param set_type pointer to member function for setting type
	  @returns true if a starting/stopping section was found and parsed
	 */
	bool starting_or_stopping(
		yaml_node_t *n,
		const std::string &name,
		const std::function<void (const std::string &)> &error,
		const std::filesystem::path &hier_name,
		std::string &command,
		time_t &timeout,
		std::unordered_set<std::string> &before,
		std::unordered_set<std::string> &after,
		proc_containerObj &new_container,
		bool (proc_containerObj::*set_type)(const std::string &));

	/*! Load container configuration from a YAML node.

	  @param unit_path path to the unit file
	  @param nc new container factory/callback
	  @param key the YAML key being processed
	  @param n the YAML node to parse
	  @param o override configuration
	  @param parsed_sigterm_notify output: whether sigterm-notify was parsed
	  @param error error callback function
	  @returns true if the container was successfully loaded
	 */
	bool load_container(
		const std::filesystem::path &unit_path,
		const proc_new_container &nc,
		const std::string &key,
		yaml_node_t *n,
		const proc_override &o,
		bool &parsed_sigterm_notify,
		const std::function<void (const std::string &)> &error);
};

#endif
