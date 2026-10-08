/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "configdirs.h"
#include "connect_vera_priv.H"
#include "connect_vera_priv_session.H"
#include "privrequest.H"
#include "verac.h"
#include <errno.h>
#include <optional>
#include <string.h>
#include <tuple>

std::optional<
	std::tuple<external_filedesc,
	std::vector<external_filedesc>>
> request_vera_priv_session()
{
        std::vector<external_filedesc> cmdfds{tmpfile_filedesc()};

        if (!cmdfds[0]->write_all(VERA_SESSION_CMD))
                return std::nullopt;
	auto opt_sp = create_vera_socketpair();
	if (!opt_sp)
		return std::nullopt;
	auto &[a,b] = *opt_sp;

        cmdfds.push_back(b);

        return std::tuple{a, cmdfds};
}

std::tuple<std::string, external_filedesc> connect_vera_priv_session(
        const std::vector<external_filedesc> &request,
        external_filedesc_connecter connecter
)
{
        auto ret=request_vera_priv_session();

        if (!ret)
                return std::tuple{strerror(errno), external_filedesc{}};

        auto &[a, cmdfds] = *ret;

        // Connect to the vera daemon, send the session command.

        auto cmdfd=(*connecter)();

        if (!cmdfd || !request_send_fd(cmdfd, cmdfds))
                return std::tuple{strerror(errno), external_filedesc{}};

        // cmdfds has a reference to the other end of the connecting socket.
        // unit tests run an execution thread, so we need to release the
        // reference to it here in order to result in the socket getting an
        // end-of-file when the request to the simulated daemon is finished.

        cmdfds.clear();

        // Check if the command succeeded.

        auto err=cmdfd->readln();

        if (!err.empty())
                return std::tuple{err, external_filedesc{}};

        // Now, send the actual command to the simulated session daemon.
        if (!request_send_fd(a, request))
                return std::tuple{strerror(errno), external_filedesc{}};

        return std::tuple{"", a};
}
