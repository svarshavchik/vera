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

static bool proc_validpath(const std::string &path)
{
	char lastchar=0;

	for (char c: path)
	{
		if (!
		    ((c & 0x80)  ||
		     c == '/' ||
		     c == ' ' || c == '.' || c == '-' ||
		     (c >= '0' && c <= '9') ||
		     (c >='A' && c <= 'Z') || (c >= 'a' && c <= 'z')))
			return false;

		if (c == '/' || // ???
		    c == '.' ||
		    c == ' ' ||
		    c == '-')
			if (c == lastchar)
				return false;

		lastchar=c;
	}

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
		      const std::function<void (const std::string &,
						const std::string &,
						bool)> &found,
		      const std::function<void (const std::string &,
						const std::string &)> &invalid,
		      const std::function<void (const std::string &)> &visited)
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

		auto localfilename=config_global / relative_filename;

		bool overridden=false;

		for (const auto &file : std::array<std::filesystem::path, 2>{
				config_local / relative_filename,
				config_override / relative_filename
			})
		{
			auto s=std::filesystem::status(file, ec);

			if (!ec)
			{
				if (s.type() ==
				    std::filesystem::file_type::directory)
				{
					invalid(file.lexically_normal(),
						_("ignoring directory"));
				}
				else
				{
					localfilename=file;
					overridden=true;
				}
			}

			if (config_local == config_override)
				break; // Garbage collection, etc...
		}


		found(localfilename.lexically_normal(),
		      relative_filename.lexically_normal(),
		      overridden);
	}
}

void proc_find(const std::string &config_global,
	       const std::string &config_local,
	       const std::string &config_override,
	       const std::function<void (const std::string &,
					 const std::string &,
					 bool)> &found,
	       const std::function<void (const std::string &,
					 const std::string &)> &invalid)
{
	proc_find(config_global, config_local, config_override, ".",
		  found,
		  invalid,
		  []
		  (const std::string &)
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
		  (const std::string &,
		   const std::string &,
		   bool)
		  {
		  },
		  [&]
		  (const std::string &path,
		   const std::string &error_message)
		  {
			  message((unlink(path.c_str()) == 0 ?
				   _("removed: ") :
				   _("could not remove: "))
				  + path + ": " + error_message);
		  },
		  [&]
		  (const std::string &path)
		  {
			  if (rmdir(path.c_str()) == 0)
			  {
				  message(_("removed empty directory: ") +
					  path);
			  }
		  });

	for (const auto &string_ptr: std::array<const std::string *, 2>{
			{&config_local, &config_override}
		} )
	{
		proc_find(*string_ptr, config_global, config_global, ".",
			  [&]
			  (const std::string &path,
			   const std::string &error_message,
			   bool overriden)
			  {
				  if (overriden)
					  return; // Global/main exists.

				  message((unlink(path.c_str()) == 0 ?
					   _("stale (removed): ") :
					   _("could not remove stale entry: "))
					  + path + ": " + error_message);
			  },
			  [&]
			  (const std::string &path,
			   const std::string &error_message)
			  {
				  message((unlink(path.c_str()) == 0 ?
					   _("removed: ") :
					   _("could not remove: "))
					  + path + ": " + error_message);
			  },
			  [&]
			  (const std::string &path)
			  {
				  if (rmdir(path.c_str()) == 0)
				  {
					  message(_("removed empty directory: ")
						  + path);
				  }
			  });
	}
}
