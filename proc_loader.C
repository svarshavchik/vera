/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "proc_loader.H"
#include "messages.H"
#include <algorithm>
#include <filesystem>
#include <array>
#include <string>
#include <unistd.h>
#include <fstream>
#include <algorithm>
#include <optional>
#include <unordered_set>
#include <yaml.h>

#define SPECIAL(c) \
	((c) == '/' ||					\
	 (c) == ' ' || (c) == '.' || (c) == '-')

static bool proc_validpath(const std::string &path)
{
	if (path.size() == 0 || SPECIAL(*path.c_str()))
		return false;

	char lastchar=0;

	for (char c: path)
	{
		if (!
		    ((c & 0x80)  ||
		     SPECIAL(c) ||
		     (c >= '0' && c <= '9') ||
		     (c >='A' && c <= 'Z') || (c >= 'a' && c <= 'z')))
			return false;

		if (SPECIAL(c) && c == lastchar)
			return false;

		lastchar=c;
	}

	if (SPECIAL(lastchar))
		return false;

	for (auto b=path.begin(), e=path.end(); b != e; )
	{
		auto p=b;

		b=std::find(b, e, '/');

		if (*p == '.' || *p == ' ' ||
		    b[-1] == '.' || b[-1] == ' ')
			return false;

		if (b != e)
			++b;
	}

	return true;
}

static void proc_find(const std::filesystem::path &config_global,
		      const std::filesystem::path &config_local,
		      const std::filesystem::path &config_override,
		      const std::filesystem::path &subdir,
		      const std::function<void (
			      const std::filesystem::path &,
			      const std::optional<std::filesystem::path> &,
			      const std::optional<std::filesystem::path> &,
			      const std::filesystem::path &)> &found,
		      const std::function<void (const std::filesystem::path &,
						const std::string &)> &invalid,
		      const std::function<void (const std::filesystem::path &)> &visited)
{
	auto fullglobal=config_global / subdir;

	auto fulllocal=config_local / subdir;

	auto fulloverride=config_override / subdir;

	std::error_code ec;

	std::filesystem::directory_iterator b{
		fullglobal,
		ec}, e;

	if (ec)
	{
		invalid(fullglobal, ec.message());
	}

	while (b != e)
	{
		auto entry = *b++;

		auto fullpath=entry.path().lexically_normal();

		auto filename=entry.path().filename();

		auto relative_filename=subdir / filename;

		if (!proc_validpath(relative_filename.lexically_normal()))
		{
			invalid(fullpath,
				_("ignoring non-compliant filename"));
			continue;
		}

		if (entry.is_directory())
		{
			auto subdir_name = subdir / filename;

			proc_find(config_global, config_local, config_override,
				  subdir_name,
				  found, invalid, visited);
			visited(config_global / subdir_name);
			continue;
		}


		if (!entry.is_regular_file())
		{
			invalid(fullpath, _("not a regular file"));
			continue;
		}

		auto globalfilename=config_global / relative_filename;

		std::optional<std::filesystem::path> localfilename;
		std::optional<std::filesystem::path> overridefilename;

		for (const auto &[found, file] : std::array<
			     std::tuple<std::optional<std::filesystem::path> *,
			     std::filesystem::path>,
			     2>{{
				     {
					     &localfilename,
					     (config_local /
					      relative_filename)
						     .lexically_normal(),
				     },
				     {
					     &overridefilename,
					     (config_override /
					      relative_filename)
						     .lexically_normal(),
				     }
			     }})
		{
			auto s=std::filesystem::status(file, ec);

			if (!ec)
			{
				if (s.type() ==
				    std::filesystem::file_type::directory)
				{
					invalid(file,
						_("ignoring directory"));
				}
				else
				{
					*found=file;
				}
			}
		}


		found(globalfilename.lexically_normal(),
		      localfilename,
		      overridefilename,
		      relative_filename.lexically_normal());
	}
}

void proc_find(const std::string &config_global,
	       const std::string &config_local,
	       const std::string &config_override,
	       const std::function<void (
		       const std::filesystem::path &,
		       const std::optional<std::filesystem::path> &,
		       const std::optional<std::filesystem::path> &,
		       const std::filesystem::path &)> &found,
	       const std::function<void (const std::filesystem::path &,
					 const std::string &)> &invalid)
{
	proc_find(config_global, config_local, config_override, ".",
		  found,
		  invalid,
		  []
		  (const std::filesystem::path &)
		  {
		  });
}

void proc_gc(const std::string &config_global,
	     const std::string &config_local,
	     const std::string &config_override,
	     const std::function<void (const std::string &message)> &message)
{
	proc_find(config_global, config_local, config_override, ".",
		  []
		  (const auto &,
		   const auto &,
		   const auto &,
		   const auto &)
		  {
		  },
		  [&]
		  (const std::filesystem::path &path,
		   const std::string &error_message)
		  {
			  message((unlink(path.c_str()) == 0 ?
				   _("removed: ") :
				   _("could not remove: "))
				  + static_cast<std::string>(path)
				  + ": " + error_message);
		  },
		  [&]
		  (const std::filesystem::path &path)
		  {
			  if (rmdir(path.c_str()) == 0)
			  {
				  message(_("removed empty directory: ") +
					  static_cast<std::string>(path));
			  }
		  });

	// What we do now is pretend that the local or the override directory
	// is the global directory, and the real global directory is the
	// override directory. So if we get a callback specifying that
	// something is overridden, this indicates that a real global file
	// exists; but if it's not overridden this must be a stale local or
	// override file.

	for (const auto &string_ptr: std::array<const std::string *, 2>{
			{&config_local, &config_override}
		} )
	{
		proc_find(*string_ptr, config_global, config_global, ".",
			  [&]
			  (const auto &globalpath,
			   const auto &localpath,
			   const auto &overridepath,
			   const std::filesystem::path &relpath)
			  {
				  if (localpath)
					  return; // Global/main exists.

				  message((unlink(globalpath.c_str()) == 0 ?
					   _("stale (removed): ") :
					   _("could not remove stale entry: "))
					  + static_cast<std::string>(globalpath)
					  + ": "
					  + static_cast<std::string>(relpath)
				  );
			  },
			  [&]
			  (const std::filesystem::path &path,
			   const std::string &error_message)
			  {
				  message((unlink(path.c_str()) == 0 ?
					   _("removed: ") :
					   _("could not remove: "))
					  + static_cast<std::string>(path)
					  + ": "
					  + error_message);
			  },
			  [&]
			  (const std::filesystem::path &path)
			  {
				  if (rmdir(path.c_str()) == 0)
				  {
					  message(_("removed empty directory: ")
						  + static_cast<std::string>(
							  path
						  ));
				  }
			  });
	}
}

namespace {
#if 0
}
#endif

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

int read_handler(void *ptr,
		 unsigned char *buf,
		 size_t size,
		 size_t *size_read)
{
	auto obj=reinterpret_cast<yaml_parser_info *>(ptr);

	obj->input_file.read(reinterpret_cast<char *>(buf), size);

	*size_read=obj->input_file.gcount();

	return obj->input_file.eof() ? 1:0;
}

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

	parsed_yaml(const parsed_yaml &)=delete;

	parsed_yaml &operator=(const parsed_yaml &)=delete;

	/*! Parse a YAML map

	  Repeatedly invokes the key_value callback with the key name, and
	  the yaml_node_t for the key's value.

	  The key's name is translated to ASCII lowercase, and incidentally
	  has the leading and trailing whitespace trimmed.

	 */

	bool parse_map(yaml_node_t *n,
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

			std::transform(
				keys.begin(),
				keys.end(),
				keys.begin(),
				[]
				(char c)
				{
					if (c >= 'A' && c <= 'Z')
						c += 'a'-'A';
					return c;
				});
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

				std::filesystem::path spath{*s};

				if (spath.is_absolute())
				{
					auto rel=spath.relative_path();

					if (!proc_validpath(rel))
					{
						error(*s +
						      _(": non-compliant name"))
							;
						return false;
					}
					requirements.insert(rel);
					return true;
				}

				std::string new_path =
					(hier_name.parent_path() / *s)
					.lexically_normal();

				// Drop any trailing / that lexically_normal()
				// might produce.

				if (!new_path.empty() &&
				    new_path.back() == '/')
					new_path.pop_back();

				if (!proc_validpath(new_path))
				{
					error(new_path +
					      _(": non-compliant name"));
					return false;
				}

				requirements.insert(new_path);
				return true;
			},
			error);
	}

	bool starting_or_stopping(
		yaml_node_t *n,
		const std::string &name,
		const std::function<void (const std::string &)> &error,
		const std::filesystem::path &hier_name,
		std::string &command,
		time_t &timeout,
		std::unordered_set<std::string> &before,
		std::unordered_set<std::string> &after)
	{
		bool found_command=false;
		bool found_timeout=false;

		return parse_map(
			n,
			name,
			[&](const std::string &key, auto n,
			    auto &error)
			{
				if (key == "command")
				{
					if (found_command)
					{
						error(name +
						      _(": multiple "
							"\"command\"s"));
						return false;
					}
					found_command=true;

					auto s=parse_scalar(
						n,
						name + "/command",
						error);

					if (!s)
						return false;

					command=*s;
					return true;
				}

				if (key == "timeout")
				{
					if (found_timeout)
					{
						error(name +
						      _(": multiple "
							"\"timeout\"s"));
						return false;
					}
					found_timeout=true;

					std::string timeout_name =
						name + "/timeout";

					auto s=parse_scalar(
						n,
						timeout_name,
						error);

					if (!s)
						return false;

					timeout=0;

					for (char c:*s)
					{
						if (c < '0' || c > '9')
						{
							error(timeout_name +
							      _(": invalid "
								"timeout value")
							);
							return false;
						}

						timeout *= 10;
						timeout += c-'0';

						if (timeout > 3600)
						{
							error(timeout_name +
							      _(": invalid "
								"timeout value")
							);
						}
					}

					return true;
				}

				if (key == "before")
				{
					return parse_requirements(
						n,
						name + "/before",
						error,
						hier_name,
						before
					);
				}

				if (key == "after")
				{
					return parse_requirements(
						n,
						name + "/after",
						error,
						hier_name,
						before
					);
				}
				return true;
			},
			error);
	}
};

#if 0
{
#endif
}

proc_new_container_set proc_load(
	std::istream &input_file,
	const std::string &filename,
	const std::filesystem::path &relative_path,
	const std::function<void (const std::string &)> &error)
{
	proc_new_container_set results;

	yaml_parser_info info{input_file};

	if (!info.initialized)
	{
		error(filename + _(": YAML parser initialization failure"));
		return results;
	}

	bool found_version_tag=false;

	while (1)
	{
		parsed_yaml parsed{info, filename, error};

		if (!parsed.initialized)
		{
			if (parsed.empty)
				break;
			if (results.empty())
			{
				error(filename +
				      _(": loaded document was empty"));
			}
			results.clear();
			return results;
		}

		auto n=yaml_document_get_root_node(&parsed.doc);

		if (!n)
		{
			break;
		}

		auto unit_path=relative_path;

		bool found_name=false;

		// First pass: look for "name" and "version"

		if (!parsed.parse_map(
			    n,
			    unit_path,
			    [&](const std::string &key, auto n,
			       auto &error)
			    {
				    std::string keypath=
					    (unit_path / key).lexically_normal()
					    ;

				    if (key == "name" && !found_name)
				    {
					    auto s=parsed.parse_scalar(
						    n,
						    keypath,
						    error);

					    if (!s)
						    return false;

					    if (!proc_validpath(*s))
					    {
						    error("\"" + *s + "\"" +
							  _(": non-compliant"
							    " name"));
						    return false;
					    }

					    // First YAML document's name must
					    // match the relative path's
					    // filename

					    if (results.empty() &&
						relative_path.filename() != *s)
					    {
						    error("\"" + *s +
							  _("\": does not match"
							    " its filename"));
						    return false;
					    }

					    // Second and subsequent YAML
					    // path gets appended to the
					    // unit_path.

					    if (!results.empty())
						    unit_path = (unit_path / *s)
							    .lexically_normal();
					    found_name=true;
				    }

				    if (key != "version")
					    return true;

				    return parsed.parse_sequence(
					    n,
					    keypath,
					    [&]
					    (yaml_node_t *n,
					     const std::function<
					     void (const
						   std::string &)> &error)
					    {
						    auto s=parsed.parse_scalar(
							    n,
							    keypath,
							    error);
						    if (!s)
							    return false;

						    if (*s == "1")
							    found_version_tag=
								    true;
						    return true;
					    },
					    error
				    );
			    },
			    error))
		{
			results.clear();
			return results;
		}

		std::string name{unit_path};
		auto nc=std::make_shared<proc_new_containerObj>(name);

		if (!results.insert(nc).second)
		{
			error(static_cast<std::string>(unit_path) +
			      _(": each unit must have a unique name"));
			results.clear();
			return results;
		}

		if (!parsed.parse_map(
			    n,
			    unit_path,
			    [&](const std::string &key, auto n,
			       auto &error)
			    {
				    std::string name=unit_path;

				    name += ": ";
				    name += key;

				    if (key == "requires" &&
					!parsed.parse_requirements(
						n,
						name,
						error,
						unit_path,
						nc->dep_requires))
				    {
					    return false;
				    }
				    if (key == "required-by" &&
					!parsed.parse_requirements(
						n,
						name,
						error,
						unit_path,
						nc->dep_required_by))
				    {
					    return false;
				    }

				    if (key == "starting")
				    {
					    return parsed.starting_or_stopping(
						    n,
						    name + "/starting",
						    error,
						    unit_path,
						    nc->new_container
						    ->starting_command,
						    nc->new_container
						    ->starting_timeout,
						    nc->starting_before,
						    nc->starting_after);
				    }

				    if (key == "stopping")
				    {
					    return parsed.starting_or_stopping(
						    n,
						    name + "/stopping",
						    error,
						    unit_path,
						    nc->new_container
						    ->stopping_command,
						    nc->new_container
						    ->stopping_timeout,
						    nc->stopping_before,
						    nc->stopping_after);
				    }
				    return true;
			    },
			    error))
		{
			results.clear();
			return results;
		}
	}

	if (results.empty())
	{
		return results; // Empty file
	}

	if (!found_version_tag)
	{
		error(filename + _(": did not see a \"version: 1\" tag"));
		results.clear();
	}
	return results;
}
