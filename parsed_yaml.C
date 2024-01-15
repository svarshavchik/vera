/*
** Copyright 2024 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "parsed_yaml.H"
#include "proc_loader.H"
#include <fstream>

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

			    if (!parsed.parse_map(
					n,
					this_key,
					[&]
					(const std::string &key,
					 yaml_node_t *n, const auto &error)
					{
						auto s=parsed.parse_scalar(
							n,
							this_key,
							error);

						if (!s)
							return false;

						if (key == "alias")
						{
							aliases.insert(
								std::move(*s)
							);
						}
						return true;
					},
					error))
				    return false;

			    current_runlevels.emplace(
				    key,
				    runlevel{
					    std::move(aliases)
				    });

			    return true;
		    },
		    error))
	{
		return current_runlevels;
	}

	current_runlevels=default_runlevels();

	return current_runlevels;
}

runlevels default_runlevels()
{
	return {
		{"boot", {
			},
		},
		{"shutdown", {
				{
					"0"
				},
				{
					"boot"
				},
			},
		},
		{
			"single-user", {
				{
					"1",
					"s",
					"S"
				},
				{ "boot" },
			},
		},
		{
			"multi-user", {
				{ "2" },
				{ "boot" },
			},
		},
		{
			"networking", {
				{ "3" },
				{ "boot" },
			},
		},
		{
			"graphical", {
				{ "4", "default" },
				{ "boot" },
			},
		},
		{
			"custom", {
				{ "5" },
				{ "boot" },
			},
		},
		{
			"reboot", {
				{ "6" },
				{
					"boot"
				},
			}
		},
	};
}
