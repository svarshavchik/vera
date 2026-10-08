/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "privrequest.H"
#include "log.H"

#include <array>
#include <cstring>
#include <filesystem>
#include <string_view>

namespace {
constexpr std::array<const char *, 2> services = {"vera", "vlad"};
}

void update_binlinks(std::string_view sbindir)
{
	for (auto s : services)
	{
		std::string c;
		c.reserve(sbindir.size() + 1 + std::strlen(s) + 1);
		c.append(sbindir.data(), sbindir.size());
		c += '/';
		c += s;

		std::string i;
		i.reserve(c.size() + 5);
		i.append(c);
		i += "-inst";

		std::error_code ec;

		if (!std::filesystem::exists(i, ec))
		{
			if (ec)
			{
				log_message(
					"update_binlinks: exists(" + i
					+ ") failed: " + ec.message()
				);
			}
			continue;
		}

		if (std::filesystem::equivalent(i, c, ec))
		{
			continue;
		} else
		{
			if (ec)
			{
				log_message(
					"update_binlinks: equivalent(" + i
					+ ", " + c + ") failed: " + ec.message()
				);
			}
		}

		std::string t = c;
		t += ".tmp";

		std::filesystem::remove(t, ec);

		std::filesystem::create_hard_link(i, t, ec);
		if (ec)
		{
			log_message(
				"update_binlinks: create_hard_link(" + i
				+ ", " + t + ") failed: " + ec.message()
			);
			continue;
		}

		std::filesystem::rename(t, c, ec);
		if (ec)
		{
			std::filesystem::remove(t, ec);
			log_message(
				"update_binlinks: rename(" + t
				+ ", " + c + ") failed: " + ec.message()
			);
		}
	}
}
