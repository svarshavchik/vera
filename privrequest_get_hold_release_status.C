/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "privrequest.H"
#include "connect_vera_priv.H"
#include "connect_vera_priv_session.H"

std::string get_hold_release_status(const external_filedesc &efd)
{
	return efd->readln();
}

std::string do_hold(
        bool session,
        const std::string &name,
        const std::string &reason
)
{
        std::vector<external_filedesc> cmdfds{tmpfile_filedesc()};

        if (!request_hold(cmdfds[0], name, reason))
                return _("Failed to request hold");

        external_filedesc cmdfd;

        if (session)
        {
                auto [error, sesscmdfd] = connect_vera_priv_session(
                        cmdfds,
                        &connect_vera_priv
                );

                if (!error.empty())
                        return error;
                cmdfd = std::move(sesscmdfd);
        }
        else
        {
                cmdfd = connect_vera_priv();

                if (!request_send_fd(cmdfd, cmdfds))
                        return message_ftsfd();
        }

        return get_hold_release_status(cmdfd);
}

std::string do_release(bool session, const std::string &name)
{
        std::vector<external_filedesc> cmdfds{tmpfile_filedesc()};

        if (!request_release(cmdfds[0], name))
                return _("Failed to request release");

        external_filedesc cmdfd;

        if (session)
        {
                auto [error, sesscmdfd] = connect_vera_priv_session(
                        cmdfds,
                        &connect_vera_priv
                );

                if (!error.empty())
                        return error;
                cmdfd = std::move(sesscmdfd);
        }
        else
        {
                cmdfd = connect_vera_priv();

                if (!request_send_fd(cmdfd, cmdfds))
                        return message_ftsfd();
        }

        return get_hold_release_status(cmdfd);
}