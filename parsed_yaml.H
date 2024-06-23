/*
** Copyright 2024 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef parsed_yaml_H
#define parsed_yaml_H

#include "config.h"
#include "proc_containerfwd.H"
#include <yaml.h>
#include <istream>
#include <sstream>
#include <functional>
#include <optional>
#include <filesystem>
#include <unordered_set>
#include <algorithm>
#include <string>
#include "messages.H"

/*! Validate a container name

Enforces naming conventions.

 */

bool proc_validpath(const std::string_view &path);

extern "C" int read_handler(void *,
			    unsigned char *,
			    size_t size,
			    size_t *size_read);

/*! Lightweight wrapper for a yaml_parser_t

RAII wrapper for a yaml_parser_t, that parses YAML from a std::istream.
The constructor takes a std::istream parameter. Check initialized after
construction to determine if the construction succeeded.

The copy constructor and assignment operator are deleted, yaml_parser_t
is RAII-managed.

 */

struct yaml_parser_info {
	std::istream &input_file;

	bool initialized=false;
	yaml_parser_t parser;

	yaml_parser_info(const yaml_parser_info &)=delete;
	yaml_parser_info &operator=(const yaml_parser_info &)=delete;

	yaml_parser_info(std::istream &input_file)
		: input_file{input_file}
	{
		if (!yaml_parser_initialize(&parser))
			return;
		initialized=true;
		yaml_parser_set_input(&parser,
				      read_handler,
				      reinterpret_cast<void *>(this));
	}

	~yaml_parser_info()
	{
		if (initialized)
		{
			yaml_parser_delete(&parser);
		}
	}
};

/*! Lightweight wrapper for a yaml_document_t

The constructor takes a yaml_parser_info for a parameter and parses a
YAML document out of it.

After construction, initialized indicates whether the document was parsed.
The remaining parameters to the constructor

- filename: for including in error messages

- a callback for an error message.
*/

struct parsed_yaml {

	bool initialized=false;
	bool empty=false;

	yaml_document_t doc{};

	parsed_yaml(yaml_parser_info &info,
		    const std::string &filename,
		    const std::function<void (const std::string &)> &error)
	{
		if (yaml_parser_load(&info.parser, &doc))
		{
			initialized=true;
			return;
		}

		if (info.parser.error == YAML_NO_ERROR)
		{
			empty=true;
			return;
		}

		std::ostringstream o;

		o << filename << ": " << info.parser.problem
		  << _(": line ") << info.parser.problem_mark.line
		  << _(", column ") << info.parser.problem_mark.column;

		error(o.str());
	}

	~parsed_yaml()
	{
		if (initialized)
		{
			yaml_document_delete(&doc);
		}
	}

	static void lc(std::string &s)
	{
		std::transform(
			s.begin(),
			s.end(),
			s.begin(),
			[]
			(char c)
			{
				if (c >= 'A' && c <= 'Z')
					c += 'a'-'A';
				return c;
			});
	}

	parsed_yaml(const parsed_yaml &)=delete;

	parsed_yaml &operator=(const parsed_yaml &)=delete;

	/*! Parse a YAML map

	  Repeatedly invokes the key_value callback with the key name, and
	  the yaml_node_t for the key's value.

	  The key's name is translated to ASCII lowercase, and incidentally
	  has the leading and trailing whitespace trimmed.

	 */

	bool parse_map(yaml_node_t *n,
		       bool case_sensitive,
		       const std::string &name,
		       const std::function<bool (
			       const std::string &,
			       yaml_node_t *,
			       const std::function<void (const std::string &
						 )> &)> &key_value,
		       const std::function<void (const std::string &)> &error)
	{
		if (!n || n->type != YAML_MAPPING_NODE)
		{
			error(name +
			      _(": bad format, expected a key/value map"));
			return false;
		}

		for (auto b=n->data.mapping.pairs.start,
			     e=n->data.mapping.pairs.top; b != e; ++b)
		{
			auto key=parse_scalar(
				yaml_document_get_node(&doc, b->key),
				name,
				error);

			if (!key)
				return false;

			auto &keys=*key;

			if (!case_sensitive)
				lc(keys);

			if (!key_value(keys,
				       yaml_document_get_node(&doc, b->value),
				       error))
				return false;
		}

		return true;
	}

	/*! Parse a YAML map

	  Repeatedly invokes the value callback with a YAML node for each
	  value in the sequence.

	  Invokes the value callback one time if the passed-in YAML node
	  is a scalar.

	 */

	bool parse_sequence(
		yaml_node_t *n,
		const std::string &name,
		const std::function<bool (
				    yaml_node_t *,
				    const std::function<void (
						  const std::string &
					  )> &)> &value,
		const std::function<void (const std::string &)> &error)
	{
		if (n && n->type == YAML_SCALAR_NODE)
			return value(n, error);

		if (!n || n->type != YAML_SEQUENCE_NODE)
		{
			error(name +
			      _(": bad format, expected a sequence (list)"));
			return false;
		}

		for (auto b=n->data.sequence.items.start,
			     e=n->data.sequence.items.top; b != e; ++b)
		{
			auto ns=yaml_document_get_node(&doc, *b);

			if (ns && !value(ns, error))
				return false;
		}

		return true;
	}

	/*! Parse a YAML scalar string.

	  Return std::nullopt if the passed-in YAML node is not a scalar node.

	  Automatically trims off leading and trailing whitespace.

	 */

	std::optional<std::string> parse_scalar(
		yaml_node_t *n,
		const std::string &name,
		const std::function<void (const std::string &)> &error)
	{
		if (!n || n->type != YAML_SCALAR_NODE)
		{
			error(name +
			      _(": bad format, non-scalar map key"));
			return std::nullopt;
		}

		auto b=reinterpret_cast<char *>(n->data.scalar.value);

		auto e=b+n->data.scalar.length;

		return std::string{b, e};
	}

	/*!
	  Parse a YAML scalar string into a std::string

	  Overload that returns a bool success indicator. An additional
	  std::string parameter gets passed by reference. It receives
	  the read YAML scalar string if this succeeds.
	 */
	bool parse_scalar(
		yaml_node_t *n,
		const std::string &name,
		const std::function<void (const std::string &)> &error,
		std::string &ret)
	{
		auto s=parse_scalar(n, name, error);

		if (!s)
			return false;

		ret=*s;
		return true;
	}

	/*!
	  Parse a YAML scalar string into a numeric value.

	  Overload that returns a bool success indicator. An additional
	  std::string parameter gets passed by reference. It receives
	  the read YAML scalar string if this succeeds.
	 */

	template<typename T>
	std::enable_if_t<std::is_arithmetic_v<T> &&
			 !std::is_floating_point_v<T>,
			 bool>
	parse_scalar(
		yaml_node_t *n,
		const std::string &name,
		T &ret,
		const std::function<void (const std::string &)> &error
	)
	{
		auto s=parse_scalar(n, name, error);

		if (!s)
			return false;

		std::istringstream i{*s};

		i.imbue(std::locale{"C"});
		if (i >> ret)
		{
			char c;

			if (!(i >> c))
				return true;
		}

		error(name + _(": cannot parse a numeric value"));

		return false;
	}


	/*! Parse a list of requirements

	  Uses parse_sequence(), processes each value, appends it to
	  the requirements set.

	  A name with the leading "/" gets it stripped off, and placed
	  into the requirements, as is.

	 */

	bool parse_requirements(
		yaml_node_t *n,
		const std::string &name,
		const std::function<void (const std::string &)> &error,
		const std::filesystem::path &hier_name,
		std::unordered_set<std::string> &requirements)
	{
		return parse_sequence(
			n, name,
			[&]
			(yaml_node_t *n,
			 const std::function<void (const std::string &)> &error)
			{
				auto s=parse_scalar(n, name, error);

				if (!s)
					return false;

				if (!validate_hier(*s, hier_name, error))
					return false;

				requirements.insert(*s);
				return true;
			},
			error);
	}

	static bool validate_hier(
		std::string &s,
		const std::filesystem::path &hier_name,
		const std::function<void (const std::string &)> &error
	)
	{
		if (*s.c_str() == '/')
		{
			auto rel=s.substr(1);

			if (!proc_validpath(rel))
			{
				error(s + _(": non-compliant name"))
							;
				return false;
			}
			s=rel;
			return true;
		}

		std::string new_path;

		try {
			new_path = (hier_name.parent_path() / s)
				.lexically_normal();
		} catch (...) {


		}

		if (new_path.empty())
		{
			error(s +
			      _(": non-compliant name"));
			return false;
		}

		// Drop any trailing / that lexically_normal()
		// might produce.

		if (new_path.back() == '/')
			new_path.pop_back();

		if (!proc_validpath(new_path))
		{
			error(new_path +
			      _(": non-compliant name"));
			return false;
		}

		s=new_path;
		return true;
	}

	/* Parse starting/stopping section in a unit specification */
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
};

#endif
