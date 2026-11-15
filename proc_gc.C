/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "messages.H"
#include "proc_loader.H"
#include <array>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {
#if 0
}
#endif

struct proc_gc_find : public proc_find
{
	std::function<void (const std::string &message)> message;

	proc_gc_find(
		const std::string &config_global,
		const std::string &config_local,
		const std::string &config_override,
		std::function<void (const std::string &message)> message)
		: proc_find{config_global, config_local, config_override},
		  message{std::move(message)}
	{
	}

	void found(
		const std::filesystem::path &,
		const std::optional<std::filesystem::path> &,
		const std::optional<std::filesystem::path> &,
		const std::filesystem::path &) override;

	void invalid(
		const std::filesystem::path &,
		const std::string &) override;

	void finished(const std::filesystem::path &) override;
};

struct proc_stale_find : public proc_find
{
	std::function<void (const std::string &message)> message;

	proc_stale_find(
		const std::string &config_global,
		const std::string &config_local,
		const std::string &config_override,
		std::function<void (const std::string &message)> message)
		: proc_find{config_global, config_local, config_override},
		  message{std::move(message)}
	{
	}

	void found(
		const std::filesystem::path &,
		const std::optional<std::filesystem::path> &,
		const std::optional<std::filesystem::path> &,
		const std::filesystem::path &) override;

	void invalid(
		const std::filesystem::path &,
		const std::string &) override;

	void finished(const std::filesystem::path &) override;
};

void proc_gc_find::found(
	const std::filesystem::path &l,
	const std::optional<std::filesystem::path> &ignore,
	const std::optional<std::filesystem::path> &override_path,
	const std::filesystem::path &relpath)
{
	if (!override_path)
		return;

	std::ifstream i{*override_path};

	if (!i.is_open())
		return;

	bool legacy;

	auto o=read_override(*override_path, i,
				       legacy,
				       message);

	if (!legacy)
		return;

	proc_set_override(
		config_override,
		relpath,
		o,
		[]
		  (const auto &error)
		  {
			  std::cerr << error << std::endl;
		  }
	);
}

void proc_gc_find::invalid(
	const std::filesystem::path &path,
	const std::string &error_message)
{
	message((unlink(path.c_str()) == 0 ?
			 _("removed: ") :
			 _("could not remove: "))
			+ static_cast<std::string>(path)
			+ ": " + error_message);
}

void proc_gc_find::finished(
	const std::filesystem::path &path)
{
	if (path == ".")
		return;

	auto p=config_global / path;
	if (rmdir(p.c_str()) == 0)
		message(_("removed empty directory: ") +
			static_cast<std::string>(p));
}

void proc_stale_find::found(
	const std::filesystem::path &globalpath,
	const std::optional<std::filesystem::path> &localpath,
	const std::optional<std::filesystem::path> &overridepath,
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
}

void proc_stale_find::invalid(
	const std::filesystem::path &path,
	const std::string &error_message)
{
	message((unlink(path.c_str()) == 0 ?
			 _("removed: ") :
			 _("could not remove: "))
			+ static_cast<std::string>(path)
			+ ": " + error_message);
}

void proc_stale_find::finished(
	const std::filesystem::path &path)
{
	if (path == ".")
		return;

	auto p=config_global / path;
	if (rmdir(p.c_str()) == 0)
		message(_("removed empty directory: ") +
			static_cast<std::string>(path));
}

#if 0
{
#endif
}

void proc_gc(const std::string &config_global,
	     const std::string &config_local,
	     const std::string &config_override,
	     const std::function<void (const std::string &message)> &message)
{
	proc_gc_find gc_find{
		config_global,
		config_local,
		config_override,
		message
	};

	gc_find.find();

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
		proc_stale_find stale_find{
			*string_ptr,
			config_global,
			config_override,
			message
		};
		stale_find.find();
	}
}
