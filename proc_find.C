/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "messages.H"
#include "parsed_yaml.H"
#include "proc_loader.H"
#include "vvconfigdirs.H"
#include <array>
#include <dirent.h>
#include <filesystem>
#include <optional>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

proc_find::proc_find(
	std::filesystem::path config_global,
	std::filesystem::path config_local,
	std::filesystem::path config_override
) : config_global{std::move(config_global)},
	  config_local{std::move(config_local)},
	  config_override{std::move(config_override)}
{
}

void proc_find::started(const std::filesystem::path &subdir)
{
}

void proc_find::finished(const std::filesystem::path &subdir)
{
}

void proc_find::find()
{
	scan(is_pid_1() ? "." : INSTALLED_SESSIONS_PREFIX);
}

void proc_find::scan(const std::filesystem::path &subdir)
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

	started(subdir);
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
			auto subdir_name = (subdir == ".") ?
				filename : subdir / filename;

			scan(subdir_name);
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

		for (const auto &[found, file] :
			std::array<
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

	finished(subdir);
}
