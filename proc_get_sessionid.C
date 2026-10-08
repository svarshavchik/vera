/*
** Copyright 2022-2026 S. Varshavchik
** See COPYING for distribution information.
*/

#include "proc_get_sessionid.H"
#include <fstream>
#include <charconv>
#include <string>
#include <limits>

std::optional<uid_sessionid_t> proc_get_sessionid(
	pid_t pid,
	const std::function<void (const std::string &)> &error)
{
	// Read /proc/<pid>/sessionid and /proc/<pid>/loginuid
	char pidbuf[std::numeric_limits<pid_t>::digits10 + 2];
	auto [last, ec]=std::to_chars(pidbuf, pidbuf + sizeof(pidbuf), pid);

	std::string_view proc_identifier{pidbuf, (size_t)(last-pidbuf)};

	std::string proc_pid;

	// Read /proc/<pid>/sessionid to get the session id, we already know
	// the UID.

	proc_pid.clear();
	proc_pid += "/proc/";
	proc_pid += proc_identifier;
	proc_pid += "/sessionid";

	std::ifstream sessid{proc_pid};

	std::string line;

	size_t sessid_n=0;
	if (!sessid || !std::getline(sessid, line) ||
		std::from_chars(
			line.c_str(),
			line.c_str()+line.size(),
			sessid_n
		).ec != std::errc{})
	{
		error("failed to read your session id\n");
		return {};
	}
	sessid.close();

	proc_pid="/proc/";
	proc_pid.append(proc_identifier);
	proc_pid+="/loginuid";

	sessid.open(proc_pid);

	uid_t loginuid_n=-1;
	if (!sessid || !std::getline(sessid, line) ||
		std::from_chars(
			line.c_str(),
			line.c_str()+line.size(),
			loginuid_n
		).ec != std::errc{})
	{
		error("failed to read your loginuid\n");
		return {};
	}

	return uid_sessionid_t{active_session_t{loginuid_n, sessid_n}};
}
