/*
** Copyright 2024 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "messages.H"
#include "parsed_yaml.H"
#include "proc_loader.H"
#include "yaml_writer.H"
#include <algorithm>
#include <algorithm>
#include <errno.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

std::unordered_map<std::string, std::string> environconfigvars;

namespace {
	struct autoremove {

		std::string &filename;

		~autoremove()
		{
			unlink(filename.c_str());
		}
	};
};

void proc_edit(
	const std::string &config_global,
	const std::string &config_local,
	const std::string &config_override,
	const std::string &name,
	const std::function<int (const std::string &)> &do_edit,
	const std::function<std::string ()> &do_prompt
)
{
	if (!proc_validpath(name))
	{
		throw std::runtime_error(name + _(": invalid name"));
	}

	auto filename=std::filesystem::path{config_global} / name;

	std::error_code ec;
	if (!std::filesystem::exists(filename, ec))
		throw std::runtime_error{
			name + _(": does not exist")
			+ (ec ? std::string{": "} + ec.message():"")
		};

	auto localfilename=std::filesystem::path{config_local} / name;

	std::string tmpdir=std::filesystem::temp_directory_path() /
		"vera.XXXXXXXX";

	int fd=mkstemp(tmpdir.data());

	if (fd < 0)
		throw std::runtime_error{
			_("Cannot create a temporary file")
		};

	autoremove do_autoremove{tmpdir};
	close(fd);

	if (!std::filesystem::copy_file(
		    std::filesystem::exists(localfilename, ec)
		    ? localfilename:filename,
		    tmpdir,
		    std::filesystem::copy_options::overwrite_existing,
		    ec))
	{
		throw std::runtime_error{
			static_cast<std::string>(localfilename)
			+ _(": cannot create: ")
			+ ec.message()
		};
	}

	while (1)
	{
		if (do_edit(tmpdir) != 0)
			return;

		std::vector<std::string> msgs;

		if (proc_validate_and_dump(
			    tmpdir, name, config_global,
			    config_local,
			    config_override,
			    [&]
			    (auto &msg)
			    {
				    msgs.push_back(msg);
			    },
			    []
			    (const auto &)
			    {
			    }
		    ))
		{
			break;
		}
		for (auto &m:msgs)
		{
			std::cout << m << std::endl;
		}

		std::string l;

		while (1)
		{
			l=do_prompt();

			switch (*l.c_str()) {
			case 'a':
			case 'A':
			case 'r':
			case 'R':
			case 'i':
			case 'I':
				break;
			default:
				continue;
			}
			break;
		}

		switch (*l.c_str()) {
		case 'a':
		case 'A':
			return;
		case 'R':
		case 'r':
			continue;
		}
		break;
	}

	auto localfilenamedir=localfilename.parent_path();

	ec={};
	std::filesystem::create_directories(localfilenamedir, ec);

	if (ec)
	{
		throw std::runtime_error{
			static_cast<std::string>(localfilenamedir)
			+ _(": cannot create: ")
			+ ec.message()
		};
	}

	std::string tmplocalfilename=localfilename;

	tmplocalfilename += "~";

	if (!std::filesystem::copy_file(
		    tmpdir,
		    tmplocalfilename,
		    std::filesystem::copy_options::overwrite_existing,
		    ec))
	{
		throw std::runtime_error{
			tmplocalfilename
			+ _(": cannot create: ")
			+ ec.message()
		};
	}

	ec={};
	std::filesystem::rename(tmplocalfilename, localfilename, ec);
	if (ec)
	{
		throw std::runtime_error{
			static_cast<std::string>(localfilename)
			+ _(": cannot create: ")
			+ ec.message()
		};
	}
}

void proc_revert(
	const std::string &config_global,
	const std::string &config_local,
	const std::string &config_override,
	const std::string &name
)
{
	if (!proc_validpath(name))
	{
		throw std::runtime_error(name + _(": invalid name"));
	}

	auto filename=std::filesystem::path{config_local} / name;

	std::error_code ec;
	if (!std::filesystem::exists(filename, ec))
		throw std::runtime_error{
			name + _(": does not exist")
			+ (ec ? std::string{": "} + ec.message():"")
		};

	if (!std::filesystem::remove(filename, ec))
	{
		throw std::runtime_error{
			static_cast<std::string>(filename) + ": "
			+ ec.message()
		};
	}
}

proc_override proc_get_override(const std::string &config_global,
				const std::string &config_override,
				const std::string &name)
{
	if (!proc_validpath(name))
	{
		throw std::runtime_error(name + _(": invalid name"));
	}

	std::error_code ec;

	if (!std::filesystem::exists(
		    std::filesystem::path{config_global} / name, ec))
		throw std::runtime_error{
			name + _(": does not exist")
			+ (ec ? std::string{": "} + ec.message():"")
		};

	return proc_get_validated_override(config_override, name);
}

static void parse_override_file(
	proc_override &o,
	std::string &state_str,
	parsed_yaml &parsed,
	const std::string &filename,
	std::function<void (const std::string &)> error)
{
	bool found_version_tag=false;

	proc_override::resources_t resources;

	(void)parsed.parse_map(
		yaml_document_get_root_node(&parsed.doc),
		true,
		filename,
		[&]
		(const std::string &key,
		 yaml_node_t *n,
		 const auto &error)
		{
			if (key == "state")
			{
				auto value=parsed.parse_scalar(
					n,
					filename,
					error
				);

				if (value)
					state_str=*value;
			}

			if (key == "resources")
			{
				return parsed.parse_map(
					n,
					true,
					filename,
					[&]
					(const std::string &key,
					 yaml_node_t *n,
					 const auto &error)
					{
						auto value=parsed.parse_scalar(
							n,
							filename,
							error
						);

						if (!value)
							return false;

						resources.emplace_back(key,
								       *value);
						return true;
					},
					error);


			}
			if (key == "version")
				return parsed.parse_version_1(
					n, key, error,
					found_version_tag
				);
			return true;
		},
		error
	);

	o.install_resources(std::move(resources));

	if (!found_version_tag)
	{
		error(_("did not see a \"version: 1\" tag"));
	}
}

proc_override proc_get_validated_override(const std::string &config_override,
					  const std::string &name)
{
	auto filename=std::filesystem::path{config_override} / name;

	std::ifstream i{filename};

	if (!i)
		return {};

	bool ignore;

	return read_override(filename, i, ignore,
			     [&]
			     (const std::string &error)
			     {
				     throw std::runtime_error{
					     name + ": " + error
				     };
			     });
}

proc_override read_override(
	const std::string &filename,
	std::ifstream &i, bool &legacy,
	const std::function<void (const std::string &)> &error)
{
	proc_override o;
	legacy=false;

	std::string s;

	std::getline(i, s);

	if (s.find(':') != s.npos)
	{
		s.clear();
		i.seekg(0);
		// Before 1.2 this was a single, plain line with the override
		// state. It is now a YAML document.

		yaml_parser_info parser_info{i};

		if (!parser_info.initialized)
		{
			std::cerr << filename
				  << _(": YAML parser initialization failure")
				  << std::endl;
		}
		else
		{
			std::function<void (const std::string &)> ferror=[&]
				(const std::string &msg)
				{
					error(filename
					      + ": "
					      + msg);
				};

			parsed_yaml parsed{parser_info,
				filename,
				error
			};

			parse_override_file(o, s, parsed, filename, error);
		}
	}
	else
	{
		legacy=true;
	}
	if (s == "masked")
		o.set_state(proc_override::state_t::masked);

	if (s == "enabled")
		o.set_state(proc_override::state_t::enabled);

	return o;
}

void proc_set_override(
	const std::string &config_override,
	const std::string &path,
	const proc_override &o,
	const std::function<void (const std::string &)> &error)
{
	if (!proc_validpath(path))
	{
		error(path + _(": non-compliant filename"));
		return;
	}

	std::string contents;

	{
		std::ostringstream yaml_stream;

		yaml_map_t yaml_contents;

		switch (o.get_state()) {
		case proc_override::state_t::none:
			break;
		case proc_override::state_t::enabled:
			yaml_contents.emplace_back(
				std::make_shared<yaml_write_scalar>(
					"state"
				),
				std::make_shared<yaml_write_scalar>(
					"enabled"
				)
			);
			break;
		case proc_override::state_t::masked:
			yaml_contents.emplace_back(
				std::make_shared<yaml_write_scalar>(
					"state"
				),
				std::make_shared<yaml_write_scalar>(
					"masked"
				)
			);
			break;
		}

		auto &resources=o.get_resources();

		if (!resources.empty())
		{
			yaml_map_t yaml_resources;

			for (auto &[key, value]:resources)
			{
				yaml_resources.emplace_back(
					std::make_shared<yaml_write_scalar>(
						key
					),
					std::make_shared<yaml_write_scalar>(
						value
					)
				);
			}

			yaml_contents.emplace_back(
				std::make_shared<yaml_write_scalar>(
					"resources"
				),
				std::make_shared<yaml_write_map>(
					std::move(yaml_resources)
				)
			);

		}
		if (!yaml_contents.empty())
		{
			yaml_contents.emplace_back(
				std::make_shared<yaml_write_scalar>(
					"version"
				),
				std::make_shared<yaml_write_scalar>(
					"1"
				)
			);

			yaml_writer{yaml_stream}.write(
				yaml_write_map{yaml_contents}
			);
		}
		contents=yaml_stream.str();
	}

	int fd=open(static_cast<std::string>(config_override).c_str(),
		    O_RDONLY|O_DIRECTORY);

	if (fd < 0)
		throw std::system_error{ {errno, std::system_category()},
					 config_override};

	auto efd=std::make_shared<external_filedescObj>(fd);

	if (flock(fd, LOCK_EX) < 0)
		throw std::system_error{ {errno, std::system_category()},
					 config_override};
	// Create any parent directories, first.

	std::filesystem::path fullpath{config_override};

	if (contents.empty())
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

	for (auto b=path.begin(), e=path.end(), p=b; (p=std::find(p, e, '/'))
		     != e;
	     ++p)
	{
		mkdir( (fullpath / std::string{b, p}).c_str(), 0755);
	}

	auto temppath = fullpath / (path + "~");

	std::ofstream orewrite{ temppath };

	if (!orewrite)
	{
		error(static_cast<std::string>(temppath) + ": " +
		      strerror(errno));
		return;
	}

	orewrite << contents;
	orewrite.close();

	if (orewrite.fail() ||
	    rename(temppath.c_str(), (fullpath / path).c_str()))
	{
		error(static_cast<std::string>(temppath) + ": " +
		      strerror(errno));
		return;
	}
}
