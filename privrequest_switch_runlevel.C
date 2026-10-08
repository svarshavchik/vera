/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "connect_vera_priv.H"
#include "messages.H"
#include "privrequest.H"

/*
Send a runlevel switch request to the daemon
*/
std::string privrequest_switch_runlevel(
		const std::string &runlevel,
		bool waitrunlevel_flag
        )
{
	auto cmdfd=connect_vera_priv();
	std::vector<external_filedesc> cmdfds{tmpfile_filedesc()};

	if (!request_runlevel(cmdfds[0], runlevel))
		throw std::runtime_error(_("Failed to request runlevel"));

	if (!request_send_fd(cmdfd, cmdfds))
		return message_ftsfd();

	auto ret=get_runlevel_status(cmdfd);

	if (ret.empty())
        {
                if (waitrunlevel_flag)
                        wait_runlevel(cmdfd);
        }

        return ret;
}