/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "privrequest.H"
#include "messages.H"
#include "connect_vera_priv.H"
#include <iostream>

bool process_session_stop()
{
	auto [cmdfd, cmdfds]=connect_vera_priv();
	auto [stopwaita, stopwaitb]=create_vera_socketpair();

	if (!request_session_stop(cmdfds[0]))
		exit(1);
	cmdfds.push_back(std::move(stopwaitb));
        stopwaitb={};
        wrap_request_send_fd(cmdfd, cmdfds);
        cmdfds.clear();

        auto stopresult=cmdfd->readln();
        if (!stopresult.empty())
        {
                std::cerr << _("Failed to stop session: ") << stopresult
                                        << "\n" << std::flush;
                return false;
        }

        stopwaita->readln();
	return true;
}