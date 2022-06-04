/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "proc_loader.H"
#include "messages.H"
#include <algorithm>
#include <filesystem>
#include <vector>
#include <array>
#include <string>
#include <unistd.h>
#include <fstream>
#include <algorithm>
#include <optional>
#include <memory>
#include <unordered_set>
#include <errno.h>
#include <sys/stat.h>
#include <yaml.h>

#define SPECIAL(c) \
	((c) == '/' ||					\
	 (c) == ' ' || (c) == '.' || (c) == '-')

bool proc_validpath(const std::string_view &path)
{
	// cgroup directories are formed by replacing all /s with : and
	// appending one more :, so the maximum path.size() is NAME_MAX-1.
	// (this is checked later).

	if (path.size() == 0 || path.size() >= NAME_MAX
	    || SPECIAL(*path.data()))
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

		if (!proc_validpath(static_cast<const std::string &>(
					    relative_filename.lexically_normal()
				    )))
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

				std::filesystem::path spath{*s};

				if (spath.is_absolute())
				{
					auto rel=spath.relative_path();

					if (!proc_validpath(static_cast<
							    const std::string &>
							    (rel)))
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
		std::unordered_set<std::string> &after,
		proc_containerObj &new_container,
		bool (proc_containerObj::*set_type)(const std::string &))
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

					return parse_scalar(
						n,
						name + "/command",
						error,
						command);
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
						after
					);
				}

				if (key == "type")
				{
					auto v=parse_scalar(
						n,
						name + "/type",
						error);

					if (!v)
						return false;

					lc(*v);
					return (new_container.*set_type)(*v);
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

static bool proc_load_container(
	parsed_yaml &parsed,
	const std::filesystem::path &unit_path,
	const proc_new_container &nc,
	const std::string &key,
	yaml_node_t *n,
	bool enabled,
	const std::function<void (const std::string &)> &error);

proc_new_container_set proc_load(
	std::istream &input_file,
	const std::string &filename,
	const std::filesystem::path &relative_path,
	bool enabled,
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

		if (name.size()+1 > NAME_MAX)
		{
			error(name + _(": maximum size of container's name"
				       " exceeded"));
			results.clear();
			return results;
		}

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
				    return proc_load_container(
					    parsed,
					    unit_path,
					    nc,
					    key,
					    n,
					    enabled,
					    error);
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

// Parse a single container.

static bool proc_load_container(
	parsed_yaml &parsed,
	const std::filesystem::path &unit_path,
	const proc_new_container &nc,
	const std::string &key,
	yaml_node_t *n,
	bool enabled,
	const std::function<void (const std::string &)> &error)
{
	std::string name=unit_path;

	name += ": ";
	name += key;

	if (key == "description")
	{
		if (!parsed.parse_scalar(
			    n,
			    name + "/description",
			    error,
			    nc->new_container->description))
			return false;
	}

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

	// "required-by" loads dep_required_by
	//
	// "enabled" is functionally equivalent
	// to "required-by".

	if ((key == "required-by" ||
	     (key == "enabled" && enabled)
	    ) &&
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
			nc->new_container->starting_command,
			nc->new_container->starting_timeout,
			nc->starting_before,
			nc->starting_after,
			*nc->new_container,
			&proc_containerObj::set_start_type
		);
	}

	if (key == "stopping")
	{
		return parsed.starting_or_stopping(
			n,
			name + "/stopping",
			error,
			unit_path,
			nc->new_container->stopping_command,
			nc->new_container->stopping_timeout,
			nc->stopping_before,
			nc->stopping_after,
			*nc->new_container,
			&proc_containerObj::set_stop_type
		);

	}

	if (key == "restart")
	{
		return parsed.parse_scalar(
			n,
			name,
			error,
			nc->new_container
			->restarting_command);
	}

	if (key == "reload")
	{
		return parsed.parse_scalar(
			n,
			name,
			error,
			nc->new_container
			->reloading_command);
	}

	if (key == "respawn")
	{
		return parsed.parse_map(
			n, name,
			[&](const std::string &key, auto n,
			    auto &error)
			{
				if (key == "attempts")
				{
					return parsed.parse_scalar(
						n,
						name + ": attempts",
						nc->new_container
						->respawn_attempts,
						error);
				}

				if (key == "limit")
				{
					return parsed.parse_scalar(
						n,
						name + ": limit",
						nc->new_container
						->respawn_limit,
						error);
				}

				return true;
			},
			error);
	}

	return true;
}

void proc_set_override(
	const std::string &config_override,
	const std::string &path,
	const std::string &override_type,
	const std::function<void (const std::string &)> &error)
{
	if (!proc_validpath(path))
	{
		error(path + _(": non-compliant filename"));
		return;
	}

	// Create any parent directories, first.

	std::filesystem::path fullpath{config_override};

	if (override_type == "none")
	{
		unlink( (fullpath / path).c_str());

		std::filesystem::path subdir{path};

		while (subdir.has_relative_path())
		{
			subdir=subdir.parent_path();

			if (subdir.empty())
				break;

			if (rmdir( (fullpath / subdir).c_str()) < 0)
				break;
		}

		return;
	}

	if (override_type != "masked" && override_type != "enabled")
	{
		error(override_type + _(": unknown override type"));
		return;
	}

	for (auto b=path.begin(), e=path.end(), p=b; (p=std::find(p, e, '/'))
		     != e;
	     ++p)
	{
		mkdir( (fullpath / std::string{b, p}).c_str(), 0755);
	}

	auto temppath = fullpath / (path + "~");

	std::ofstream o{ temppath };

	if (!o)
	{
		error(static_cast<std::string>(temppath) + ": " +
		      strerror(errno));
		return;
	}

	o << override_type << "\n";
	o.close();

	if (o.fail() ||
	    rename(temppath.c_str(), (fullpath / path).c_str()))
	{
		error(static_cast<std::string>(temppath) + ": " +
		      strerror(errno));
		return;
	}
}

static proc_override read_override(std::istream &i)
{
	std::string s;

	std::getline(i, s);

	if (s == "masked")
		return proc_override::masked;

	if (s == "enabled")
		return proc_override::enabled;

	return proc_override::none;
}

proc_new_container_set proc_load_all(
	const std::string &config_global,
	const std::string &config_local,
	const std::string &config_override,

	const std::function<void (const std::string &)> &warning,
	const std::function<void (const std::string &)> &error
)
{
	proc_new_container_set containers;

	proc_find(config_global,
		  config_local,
		  config_override,
		  [&]
		  (const auto &global_path,
		   const auto &local_path,
		   const auto &override_path,
		   const auto &relative_path)
		  {
			  bool enabled=false;

			  if (override_path)
			  {
				  std::ifstream i{*override_path};

				  if (!i.is_open())
				  {
					  error(static_cast<std::string>(
							  *override_path
						  ) + ": " + strerror(errno));
					  return;
				  }

				  switch (read_override(i)) {
				  case proc_override::masked:
					  return;

				  case proc_override::enabled:
					  enabled=true;
					  break;
				  case proc_override::none:
					  break;
				  }
			  }

			  std::string name{
				  local_path ? *local_path:global_path
			  };

			  std::ifstream i{name};

			  if (!i.is_open())
			  {
				  error(name + ": " + strerror(errno));
				  return;
			  }
			  containers.merge(
				  proc_load(i, name, relative_path,
					    enabled, error)
			  );
		  },
		  [&]
		  (const auto &path,
		   const auto &message)
		  {
			  warning(static_cast<std::string>(path)
				  + ": " + message);
		  });

	return containers;
}

std::unordered_map<std::string, proc_override> proc_get_overrides(
	const std::string &config_global,
	const std::string &config_local,
	const std::string &config_override
)
{
	std::unordered_map<std::string, proc_override> ret;

	proc_find(config_global,
		  config_local,
		  config_override,
		  [&]
		  (const auto &global_path,
		   const auto &local_path,
		   const auto &override_path,
		   const auto &relative_path)
		  {
			  if (!override_path)
				  return;

			  std::ifstream i{*override_path};

			  if (!i.is_open())
				  return;

			  auto o=read_override(i);

			  if (o != proc_override::none)
				  ret.emplace(relative_path, o);
		  },
		  [&]
		  (const auto &, const auto &)
		  {
		  });
	return ret;
}

///////////////////////////////////////////////////////////////////////////
//
// Some basic, generic scaffolding for writing out YAML. This is used to
// update the runlevel configuration file, to specify the default runlevel

namespace {
#if 0
}
#endif

// Interface libyaml to write the YAML contents to a std::ofstream

extern "C" int write_runlevel(void *data,
			      unsigned char *buffer,
			      size_t size)
{
	auto stream=reinterpret_cast<std::ofstream *>(data);

	stream->write( reinterpret_cast<char *>(buffer), size);

	return stream->good() ? 1:0;
}

struct yaml_writer;

// Write out a YAML node.

struct yaml_write_node {

	virtual bool write(yaml_writer &w)=0;
};

// RAII wrapper for a yaml_emitter.

struct yaml_writer {

	std::ofstream &o;
	yaml_emitter_t emitter;
	bool initialized{false};

	yaml_writer(std::ofstream &o) : o{o}
	{
		if (!yaml_emitter_initialize(&emitter))
			return;
		initialized=true;

		yaml_emitter_set_output(&emitter, &write_runlevel, &o);
	}

	~yaml_writer()
	{
		if (!initialized)
		    return;

		yaml_emitter_delete(&emitter);
	}

	// Write out the top level YAML node.

	bool write(yaml_write_node &n)
	{
		yaml_event_t stream_start, stream_end;
		yaml_event_t doc_start, doc_end;

		if (!yaml_stream_start_event_initialize(
			    &stream_start,
			    YAML_ANY_ENCODING))
			return false;

		if (!yaml_emitter_emit(&emitter, &stream_start))
			return false;

		if (!yaml_document_start_event_initialize(
			    &doc_start,
			    NULL, NULL, NULL, 1))
			return false;

		if (!yaml_emitter_emit(&emitter, &doc_start))
			return false;

		if (!n.write(*this))
			return false;

		if (!yaml_document_end_event_initialize(&doc_end, 1))
			return false;

		if (!yaml_emitter_emit(&emitter, &doc_end))
			return false;

		if (!yaml_stream_end_event_initialize(&stream_end))
			return false;

		if (!yaml_emitter_emit(&emitter, &stream_end))
			return false;

		return true;
	}
};

// Write out a scalar.

// The object owns a std::string with the scalar's value.

struct yaml_write_scalar : yaml_write_node {

	std::string s;
	yaml_event_t event;

	yaml_write_scalar(std::string s) : s{std::move(s)}
	{
	}

	bool write(yaml_writer &w) override
	{
		if (!yaml_scalar_event_initialize(
			    &event, NULL, NULL,
			    reinterpret_cast<const yaml_char_t *>(s.c_str()),
			    s.size(),
			    1, 1,
			    YAML_ANY_SCALAR_STYLE))
			return false;

		if (!yaml_emitter_emit(&w.emitter, &event))
			return false;

		return true;
	}


};

// Write out a map. The map is represented as a vector of key/value tuples.

typedef std::vector<std::tuple<std::shared_ptr<yaml_write_node>,
			       std::shared_ptr<yaml_write_node>>> yaml_map_t;

struct yaml_write_map : yaml_write_node {

	yaml_event_t start_event, end_event;

	yaml_map_t map;

	yaml_write_map(yaml_map_t map) : map{std::move(map)}
	{
	}

	bool write(yaml_writer &w) override
	{
		if (!yaml_mapping_start_event_initialize(
			    &start_event,
			    NULL, NULL, 1, YAML_ANY_MAPPING_STYLE
		    ))
			return false;

		if (!yaml_emitter_emit(&w.emitter, &start_event))
			return false;

		for ( auto &[key, value] : map)
		{
			if (!key->write(w) || !value->write(w))
				return false;
		}

		if (!yaml_mapping_end_event_initialize(&end_event))
			return false;

		if (!yaml_emitter_emit(&w.emitter, &end_event))
			return false;

		return true;
	}
};

// Write out a YAML sequence. The sequence naturally gets defined as a vector.

struct yaml_write_seq : yaml_write_node {

	yaml_event_t start_event, end_event;

	std::vector<std::shared_ptr<yaml_write_node>> seq;

	yaml_write_seq(std::vector<std::shared_ptr<yaml_write_node>> seq)
		: seq{std::move(seq)}
	{
	}

	bool write(yaml_writer &w) override
	{
		if (!yaml_sequence_start_event_initialize(
			    &start_event,
			    NULL, NULL, 1, YAML_ANY_SEQUENCE_STYLE
		    ))
			return false;

		if (!yaml_emitter_emit(&w.emitter, &start_event))
			return false;

		for ( auto &value : seq)
			if (!value->write(w))
				return false;

		if (!yaml_sequence_end_event_initialize(&end_event))
			return false;

		if (!yaml_emitter_emit(&w.emitter, &end_event))
			return false;
		return true;
	}
};
#if 0
{
#endif
}

bool proc_set_runlevel_config(const std::string &configfile,
			      const runlevels &new_runlevels)
{
	// Create a temporary file, first.

	std::string tmpname = configfile + "~";

	std::ofstream o{tmpname};

	if (!o)
		return false;

	o << "# This file gets automatically updated.\n"
		"# It should not be manually updated.\n\n";

	yaml_writer writer{o};

	if (!writer.initialized)
	{
		std::cout << _("unable to initialize the YAML writer")
			  << "\n";
		return false;
	}

	yaml_map_t m;

	for (auto &[name, aliases]:new_runlevels)
	{
		std::vector<std::shared_ptr<yaml_write_node>> levels;

		levels.reserve(aliases.size());

		for (auto &a:aliases)
		{
			levels.push_back(
				std::make_shared<yaml_write_scalar>(a)
			);
		}

		m.emplace_back(std::make_shared<yaml_write_scalar>(name),
			       std::make_shared<yaml_write_seq>(
				       std::move(levels)
			       )
		);
	}

	yaml_write_map runlevel_map{ std::move(m) };

	errno=0;

	if (!writer.write(runlevel_map) || (o.close(), !o) ||
	    rename(tmpname.c_str(), configfile.c_str()))
	{
		std::cerr << configfile << ": "
			  << (errno ? strerror(errno)
			      : _("error writing out the YAML file"))
			  << "\n";
		return false;
	}

	return true;
}

runlevels default_runlevels()
{
	return {
		{"shutdown", {
			"0"
			}
		},
		{
			"single-user",	{ "1", "s", "S" },
		},
		{
			"multi-user",	{ "2" },
		},
		{
			"networking",	{ "3" },
		},
		{
			"custom",	{ "4" },
		},
		{
			"graphical",	{ "5", "default" },
		},
		{
			"reboot",	{ "6" }
		},
	};
}


runlevels proc_get_runlevel_config(
	const std::string &configfile,
	const std::function<void (const std::string &)> &error)
{
	std::ifstream input_file{configfile};

	if (!input_file)
	{
		error(configfile + ": " + strerror(errno));
		return default_runlevels();
	}

	yaml_parser_info info{input_file};

	if (!info.initialized)
	{
		error(configfile +
		      _(": YAML parser initialization failure"));
		return default_runlevels();
	}

	parsed_yaml parsed{info, configfile, error};

	if (!parsed.initialized)
	{
		error(configfile +
		      _(": loaded document was empty"));
		return default_runlevels();
	}

	runlevels current_runlevels;

	if (parsed.parse_map(
		    yaml_document_get_root_node(&parsed.doc),
		    configfile,
		    [&]
		    (const std::string &key,
		     yaml_node_t *n,
		     const auto &error)
		    {
			    std::unordered_set<std::string> aliases;

			    auto this_key = configfile + "/" + key;

			    if (!parsed.parse_sequence(
					n,
					this_key,
					[&]
					(yaml_node_t *n, const auto &error)
					{
						auto s=parsed.parse_scalar(
							n,
							this_key,
							error);

						if (!s)
							return false;
						aliases.insert(
							std::move(*s)
						);

						return true;
					},
					error))
				    return false;

			    current_runlevels.emplace(
				    key,
				    std::move(aliases));

			    return true;
		    },
		    error))
	{
		return current_runlevels;
	}

	current_runlevels=default_runlevels();

	return current_runlevels;
}

bool proc_set_runlevel_default(
	const std::string &configfile,
	const std::string &new_runlevel,
	const std::function<void (const std::string &)> &error)
{
	// First, read the current default

	auto current_runlevels=proc_get_runlevel_config(configfile, error);

	// Go through, and:
	//
	// If we find a default entry, remove it.
	//
	// When we find the new default runlevel, add a "default" alias for it.

	bool found=false;

	for (auto &[runlevel, aliases] : current_runlevels)
	{
		auto iter=aliases.find("default");

		if (iter != aliases.end())
			aliases.erase(iter);

		if (!found && (runlevel == new_runlevel ||
			       aliases.find(new_runlevel) != aliases.end()))
		{
			found=true;
			aliases.insert("default");
		}
	}

	if (!found)
	{
		error(configfile + ": " + new_runlevel + _(": not found"));
		return false;
	}

	return proc_set_runlevel_config(configfile, current_runlevels);
}
