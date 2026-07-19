/*
** Copyright 2026 S. Varshavchik
*/
#include "config.h"
#include "uid_sessionid.H"
#include "proc_loader.H"
#include <charconv>
#include <cstring>

uid_sessionid_to_chars::uid_sessionid_to_chars(const uid_sessionid_t &ssid)
{
	if (!std::visit(
		[&](const auto &v)
		{
			if constexpr (std::is_same_v<std::remove_cvref_t<
						     decltype(v)>,
					     active_session_t>)
			{
				auto ret=std::to_chars(
					buf,
					buf+sizeof(buf),
					v.uid,
					10
				);

				if (ret.ec != std::errc{} ||
					ret.ptr >= buf+sizeof(buf)-1)
				{
					return false;
				}

				*ret.ptr++='/';

				ret=std::to_chars(
					ret.ptr,
					buf+sizeof(buf),
					v.sessionid,
					10
				);

				if (ret.ec != std::errc{} ||
					ret.ptr >= buf+sizeof(buf)-1)
					return false;

				*ret.ptr=0;

				return true;
			}
			else
				return false;
		},
		ssid
	))
	// Only one possibility...
		strcpy(buf, "<installed_session>");
}

std::string uid_sessionid_to_chars::path() const noexcept
{
	std::string s;

	std::string_view ssid_string_sv=*this;
	s.reserve(sizeof(ACTIVE_SESSIONS_PREFIX)-1+ssid_string_sv.size()+1);
	s += ACTIVE_SESSIONS_PREFIX;
	s += "/" ;
	s += ssid_string_sv;

	return s;
}