/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/

#include "log.H"
#include "messages.H"
#include <string_view>
#include <locale>
#include <sstream>

std::string log_elapsed(time_t n)
{
	time_t m=n/60;
	time_t s=n%60;

	std::string_view minutes{_("m:minutes")};
	std::string_view seconds{_("s:seconds")};

	std::ostringstream o;

	o.imbue(std::locale{""});

	if (m)
	{
		o << m;
		o.write(minutes.data(),
			std::find(minutes.begin(), minutes.end(), ':')-
			minutes.begin());
	}

	if (s || m == 0)
	{
		o << s;

		o.write(seconds.data(),
			std::find(seconds.begin(), seconds.end(), ':')-
			seconds.begin());
	}
	return o.str();
}
