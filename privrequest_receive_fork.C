/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "privrequest.H"
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

std::tuple<std::vector<std::vector<char>>,
	std::unordered_map<std::string, std::string>,
	external_filedesc,
	external_filedesc,
	external_filedesc> receive_fork(
	const external_filedesc &efd
)
{
	auto [fds, ignore] = receive_fds(efd);
	if (fds.size() != 4)
		return {};
	auto &argvenvironpipe=fds[0];
	auto &pidpipe=fds[1];
	auto &releasepipe=fds[2];
	auto &outerrpipe=fds[3];

	std::string s=argvenvironpipe->read_all();

	std::tuple<std::vector<std::vector<char>>,
			std::unordered_map<std::string, std::string>> ret;
	auto &argv=std::get<0>(ret);
	auto &environconfigvars=std::get<1>(ret);

	char *b=s.data();
	char *e=s.data() + s.size();

	while (b < e)
	{
		char type=*b++;

		if (e-b < 4)
			break;
		ssize_t argsize=static_cast<unsigned char>(b[0]);

		argsize <<= 8;
		argsize|=static_cast<unsigned char>(b[1]);
		argsize <<= 8;
		argsize|=static_cast<unsigned char>(b[2]);
		argsize <<= 8;
		argsize|=static_cast<unsigned char>(b[3]);

		b += 4;

		if (e-b < argsize)
			break;
		switch (type) {
		case 'a':
			{
				// These will eventually be C strings, so
				// reserve an extra byte for the null.

				argv.push_back({});
				auto &last=argv.back();
				last.reserve(argsize+1);
				last.insert(last.end(), b, b+argsize);
			}
			break;
		default:
			break;
		case 'e':
			if (argsize < 4)
				break;
			ssize_t valuesize=static_cast<unsigned char>(b[0]);
			valuesize <<= 8;
			valuesize|=static_cast<unsigned char>(b[1]);
			valuesize <<= 8;
			valuesize|=static_cast<unsigned char>(b[2]);
			valuesize <<= 8;
			valuesize|=static_cast<unsigned char>(b[3]);

			if (valuesize > argsize-4)
				break;
			std::string key{b+4, b+argsize-valuesize};
			std::string value{b+argsize-valuesize, b+argsize};
			environconfigvars.emplace(
				std::move(key),
				std::move(value)
			);

			break;
		}

		b += argsize;
	}

	return {argv, environconfigvars, pidpipe, releasepipe, outerrpipe};
}
