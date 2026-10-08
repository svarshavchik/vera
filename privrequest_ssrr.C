/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "connect_vera_priv.H"
#include "connect_vera_priv_session.H"
#include "external_filedesc.H"
#include "forward_carbon_copy.H"
#include "messages.H"
#include "privrequest.H"
#include <string>
#include <sys/wait.h>
#include <unistd.h>

/*! Send start/stop/restart/reload request

  This function is used to send start, stop, restart and reload requests
  to the vera daemon. It establishes a connection to the daemon,
  creates a socket pair for output redirection if needed, sends the command.

  If nowait is true it returns an empty string. Otherwise it waits for the
  command to finish, and returns an empty string if the command succeeded,
  otherwise an error message.

*/

static std::string do_ssrr(
	const char *name,
	bool session,
	bool (*send_command)(const external_filedesc &, std::string),
	std::string unit,
	std::string (*get_status)(const external_filedesc &),
	bool nowait,
	int (*get_result)(const external_filedesc &)
)
{
	auto opt = setup_cmd(nowait);
	if (!opt)
		return _("Failed to create output pipe");
	auto [cmdfds, stdoutcc] = std::move(*opt);

	if (!send_command(cmdfds[0], unit))
	{
		return _("Failed to send command");
	}

	external_filedesc cmdfd;

	if (session)
	{
		auto [error, sesscmdfd] = connect_vera_priv_session(
			cmdfds,
			&connect_vera_priv
		);

		if (!error.empty())
			return error;
		cmdfd=std::move(sesscmdfd);
	}
	else
	{
		cmdfd=connect_vera_priv();

		if (!request_send_fd(cmdfd, cmdfds))
			return message_ftsfd();
	}
	auto ret=(*get_status)(cmdfd);

	if (!ret.empty())
		return ret;

	if (nowait)
		return "";

	cmdfds.clear();
	forward_carbon_copy(stdoutcc, 1);

	int v=(*get_result)(cmdfd);

	if (WIFSIGNALED(v) || WEXITSTATUS(v) != 0)
		return std::string{name} + " " + unit +
			_(": operation failed, check the log files"
			  " for more information");

	return "";
}

std::string do_start(bool session, std::string unit, bool nowait)
{
	return do_ssrr(
		"start",
		session,
		send_start,
		std::move(unit),
		get_start_status,
		nowait,
		get_start_result
	);
}

std::string do_stop(bool session, std::string unit, bool nowait)
{
	return do_ssrr(
		"stop",
		session,
		send_stop,
		std::move(unit),
		get_stop_status,
		nowait,
		wait_stop
	);
}

std::string do_restart(bool session, std::string unit, bool nowait)
{
	return do_ssrr(
		"restart",
		session,
		send_restart,
		std::move(unit),
		get_restart_status,
		nowait,
		wait_restart
	);
}

std::string do_reload(bool session, std::string unit, bool nowait)
{
	return do_ssrr(
		"reload",
		session,
		send_reload,
		std::move(unit),
		get_reload_status,
		nowait,
		wait_reload
	);
}

std::string do_stop_all(bool nowait)
{
	return do_ssrr(
		"stop-all",
		true,
		send_stop_all,
		"",
		get_stop_all_status,
		nowait,
		wait_stop_all
	);
}
