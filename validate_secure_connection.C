/*
** Copyright 2022-2026 S. Varshavchik
** See COPYING for distribution information.
*/

#include "config.h"
#include "messages.H"
#include "validate_secure_connection.H"
#include <charconv>
#include <fstream>
#include <limits>
#include <signal.h>	// IWYU pragma: keep
#include <stdlib.h>
#include <string>
#include <sys/auxv.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

[[nodiscard]] bool validate_secure_connection(
	const external_filedesc &efd,
	ucred &cred,
	const std::function<void (const std::string &)> &error
)
{
#ifdef SO_PEERPIDFD
	// Kernel 6.5 required for this
	int pidfdn = -1;
	socklen_t pidfd_len = sizeof(pidfdn);

	if (getsockopt(efd->fd, SOL_SOCKET, SO_PEERPIDFD,
			&pidfdn, &pidfd_len) < 0)
	{
		error(_("failed to read your pidfd\n"));
		return false;
	}

	auto pidfd=std::make_shared<external_filedescObj>(pidfdn);

	if (syscall(__NR_pidfd_send_signal, pidfd->fd, 0, nullptr, 0) < 0)
	{
		error("failed to send signal to pidfd\n");
		return false;
	}
#endif

	// root? Don't bother. If you have root, you're secure.

	if (cred.uid != 0)
	{
		// Read /proc/<pid>/auxv to verify secure execution mode

		char pidbuf[std::numeric_limits<pid_t>::digits10 + 2];
		auto [last, ec]= std::to_chars(
			pidbuf, pidbuf + sizeof(pidbuf), cred.pid
		);

		std::string_view pidbuf_sv{pidbuf, (size_t)(last-pidbuf)};

		std::string proc_pid;

		proc_pid.reserve(pidbuf_sv.size() + sizeof("/proc//auxv") - 1);
		proc_pid += "/proc/";
		proc_pid += pidbuf_sv;
		proc_pid += "/auxv";

		bool secure_execution=false;
		std::ifstream auxv{proc_pid};

		// Read the file structure using the platform's native unsigned
		// long word-size;

		unsigned long entry[2];
		while (auxv.read(reinterpret_cast<char*>(entry), sizeof(entry)))
		{
			if (entry[0] == AT_SECURE && entry[1] == 1)
				secure_execution=true;

			if (entry[0] == 0 && entry[1] == 0)
				break;
		}

		if (!secure_execution)
		{
			std::string err;

			err = _("user process pid " );
			err.append(pidbuf_sv);
			err += _(" is not in secure execution mode");
			error(err);
			return false;
		}
	}

	return true;
}
