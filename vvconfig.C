/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/

#include "config.h"
#include "privrequest.H"
#include "proc_container_group.H"
#include "proc_get_sessionid.H"

const char slashprocslash[] = "/proc/";

static std::string session_path=
	proc_container_group_data::get_cgroupfs_default_base_path();

// Base path to cgroups

const char *proc_container_group_data::get_cgroupfs_default_base_path()
{
	return "/sys/fs/cgroup/vera";
}

std::string_view proc_container_group_data::get_cgroupfs_base_path()
{
	return session_path;
}

void proc_container_group_data::set_session_path(std::string_view path)
{
	session_path.clear();
	session_path += get_cgroupfs_default_base_path();
	session_path += path;
}

std::string proc_container_group_data::set_session_path()
{
	std::string error_msg;

	auto uidsessionid=proc_get_sessionid(
		getpid(),
		[&error_msg](const std::string &msg){
			error_msg = msg;
		}
	);

	if (!uidsessionid)
		return error_msg;

	std::string path=cgroups_dir(
		"",
		uid_sessionid_to_chars{*uidsessionid}.path()
	);

	set_session_path(path);

	return {};
}