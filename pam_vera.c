#define _GNU_SOURCE
#include "config.h"
#include "globaldefs.c"
#include "privrequest_sendfdc.h"
#include "verac.h"
#define PAM_SM_SESSION
#include "configdirs.h"
#include <security/pam_ext.h>
#include <security/pam_modules.h>
#include <syslog.h>
#include "verac.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/*

PAM module sends a session stop command to the session manager, if there
is one.

*/

PAM_EXTERN int pam_sm_open_session(
	pam_handle_t *pamh,
	int flags,
	int argc,
	const char **argv
)
{
	/* Nothing to do when opening a session */

	return PAM_SUCCESS;
}

static void read_line(int fd, char *buf, size_t bufsize)
{
	char overflow[256];
	size_t origsize=bufsize;

	/*
	** We expect responses to fit in bufsize, terminated by a newline.
	** Just gulp down everything that we read, and see if the last
	** received character is a newline.
	*/
	while (bufsize > 0)
	{
		ssize_t n = read(fd, buf, bufsize);

		if (n <= 0)
		{
			*buf=0;
			return;
		}

		if (buf[n-1] == '\n')
		{
			buf[n-1]=0;
			return;
		}

		buf += n;
		bufsize -= n;
	}

	// We better have had space for at least one byte. The response
	// overflowed. Change the last character in the buffer to '\0', then
	// swallow the rest of the response and any further data.

	if (origsize)
		buf[-1]=0;

	while (1)
	{
		ssize_t n=read(fd, overflow, sizeof(overflow));
		if (n<=0) break;
		for (ssize_t i=0; i<n; i++)
		{
			if (overflow[i] == '\n')
			{
				break;
			}
		}
	}
}

PAM_EXTERN int pam_sm_close_session(
	pam_handle_t *pamh,
	int flags,
	int argc,
	const char **argv
)
{
	const char *user = 0;
	int sessionfd, stopallfd, sv[2];
	char res1[256], res2[256];
	const char *s1 = VERA_SESSION_CMD;
	const char *s2 = VERA_STOP_ALL_CMD;

	if (pam_get_user(pamh, &user, 0) != PAM_SUCCESS || user == 0)
	{
		user = "unknown";
	}

	pam_syslog(pamh, LOG_INFO, "vera session shutdown for user: %s", user);

	/*
	** This reimplements the scaffolding of the session-stop command,
	** because we can't reliably pull in C++ code in a dlopened PAM module.
	**
	** Several key differences.
	**
	** 1) Failure to connect to PRIVCMDSOCKET is not an error, vera may
	**    not be running.
	**
	** 2) A session manager may not be running for the user.
	**
	** 3) We do not forward the output of the session-stop command.
	*/

	/*
	** Create memfds with the VERA_SESSION_CMD and VERA_STOP_ALL_CMD
	*/
	sessionfd = memfd_create("vera socket", MFD_CLOEXEC);
	stopallfd = memfd_create("vera socket", MFD_CLOEXEC);

	if (sessionfd < 0 || stopallfd < 0)
	{
		if (sessionfd >= 0) close(sessionfd);
		if (stopallfd >= 0) close(stopallfd);
		pam_syslog(pamh, LOG_ERR, "vera pam: memfd_create failed");
		return PAM_SUCCESS;
	}

	if (write(sessionfd, s1, strlen(s1)) < 0 ||
		write(stopallfd, s2, strlen(s2)) < 0)
	{
		close(sessionfd);
		close(stopallfd);
		pam_syslog(pamh, LOG_ERR, "vera pam: write to memfd failed");
		return PAM_SUCCESS;
	}

	/* Create the socket for the session command */
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
	{
		close(sessionfd);
		close(stopallfd);
		pam_syslog(pamh, LOG_ERR, "vera pam: socketpair failed");
		return PAM_SUCCESS;
	}

	/*
	** Connect to pid 1, send the session command and one end of
	** the session socket pair.
	*/
	int srv = connect_sun_socket(PRIVCMDSOCKET);
	if (srv < 0)
	{
		close(sessionfd);
		close(stopallfd);
		close(sv[0]);
		close(sv[1]);
		return PAM_SUCCESS;
	}

	int sendfds[2] = { sessionfd, sv[0] };
	if (send_fds(srv, sendfds, 2) < 0)
	{
		close(srv);
		close(sessionfd);
		close(sv[0]);
		close(sv[1]);
		pam_syslog(pamh, LOG_ERR, "vera pam: sendmsg failed");
		return PAM_SUCCESS;
	}
	close(sv[0]);

	/*
	** Check if the session command succeeded, connected to session
	** manager
	*/
	read_line(srv, res1, sizeof(res1));

	if (res1[0] || send_fds(sv[1], &stopallfd, 1) < 0)
	{
		close(srv);
		close(sv[1]);
		close(sessionfd);
		close(stopallfd);

		if (res1[0] == 0)
			pam_syslog(pamh, LOG_ERR,
				"vera pam: send_fds stopallfd failed");
		return PAM_SUCCESS;
	}

	read_line(sv[1], res2, sizeof(res2));
	close(sv[1]);

	close(srv);

	if (res2[0] != 0)
		pam_syslog(pamh, LOG_ERR,
			"vera pam: session command failed: %s", res2);
	return PAM_SUCCESS;
}