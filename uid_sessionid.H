/*
** Copyright 2022-2024 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef uid_sessionid_H
#define uid_sessionid_H

#include <variant>
#include <functional>
#include <limits>
#include <string_view>
#include <sys/types.h>

struct installed_session_t{
	bool operator==(const installed_session_t &other) const noexcept
	{
		return true;
	}

};
struct active_session_t{
	uid_t uid;
	size_t sessionid;
	bool operator==(const active_session_t &other) const noexcept
	{
		return uid == other.uid && sessionid == other.sessionid;
	}
};

struct extract_sessionid {

	size_t operator()(size_t sessionid) const noexcept
	{
		return sessionid;
	}

	size_t operator()(const active_session_t &s) const noexcept
	{
		return s.sessionid;
	}

	size_t operator()(const struct installed_session_t &) const noexcept
	{
		return 0;
	}
};

/*! A session identifier is a combination of a user id and a session id number

The session id number is really the only identifier, we attach the user id
to it so that uid/sesssionid ends up collecting all sessions for the same
user in a single hierarchy. The user id is the uid of the user that started
the session.

For convenience, the session id number alone can be used in lookups
where the user id is not important. So, we provide transparent overloads
of std::hash and std::equal_to that pay attention only to the session id
number.
*/

typedef std::variant<installed_session_t, active_session_t> uid_sessionid_t;

namespace std
{
	template<> struct hash<installed_session_t> {
		size_t operator()(const installed_session_t &) const noexcept
		{
			return std::hash<size_t>{}(0);
		}
	};

	template<>
	struct hash<active_session_t>
	{
		size_t operator()(size_t sessionid) const noexcept
		{
			return hash<size_t>{}(sessionid);
		}

		size_t operator()(const active_session_t &val) const noexcept
		{
			return hash<size_t>{}(val.sessionid);
		}
	};

	template<>
	struct hash<uid_sessionid_t> {
		typedef void is_transparent;

		size_t operator()(size_t sessionid) const noexcept
		{
			return std::hash<size_t>{}(sessionid);
		}

		size_t operator()(const active_session_t &s) const noexcept
		{
			return std::hash<active_session_t>{}(s);
		}

		size_t operator()(const uid_sessionid_t &s) const noexcept
		{
			return std::visit(
				[](auto const &v) {
					typedef std::remove_cvref_t<
						decltype(v)> type;

					return hash<type>{}(v);
				},
				s
			);
		}
	};

	template<>
	struct equal_to<installed_session_t> {
		typedef void is_transparent;

		bool operator()(size_t, installed_session_t) const noexcept
		{
			return false;
		}

		bool operator()(installed_session_t, size_t) const noexcept
		{
			return false;
		}

		bool operator()(const installed_session_t &a,
				const installed_session_t &b) const noexcept
		{
			return true;
		}
	};

	template<>
	struct equal_to<active_session_t>
	{
		typedef void is_transparent;

		bool operator()(size_t sessionid,
				const active_session_t &b) const noexcept
		{
			return sessionid == b.sessionid;
		}

		bool operator()(installed_session_t,
				const active_session_t &b) const noexcept
		{
			return false;
		}

		bool operator()(const active_session_t &a,
				size_t sessionid) const noexcept
		{
			return operator()(sessionid, a);
		}

		bool operator()(const active_session_t &a,
				installed_session_t) const noexcept
		{
			return false;
		}

		bool operator()(const active_session_t &a,
				const active_session_t &b) const noexcept
		{
			return a.sessionid == b.sessionid;
		}
	};

	template<>
	struct equal_to<uid_sessionid_t>
	{
		typedef void is_transparent;

		bool operator()(const uid_sessionid_t &a,
				const uid_sessionid_t &b) const noexcept
		{
			return std::visit(
				[&](auto const &va) {
					typedef std::remove_cvref_t<
						decltype(va)> typea;

					return std::visit(
						[&](const auto &vb)
						{
							typedef std::remove_cvref_t<
								decltype(vb)
							> typeb;

							if constexpr (std::is_same_v<
								typea, typeb
							>)
							{
								return equal_to<
									typea
								>{}(va, vb);
							}
							else
								return false;
						},
						b
					);
				},
				a
			);
		}

		bool operator()(size_t sessionid,
				const uid_sessionid_t &b) const noexcept
		{
			return std::visit(
				[sessionid](auto const &vb) {
					typedef std::remove_cvref_t<
						decltype(vb)
					> typeb;
					return equal_to<
						typeb
					>{}(sessionid, vb);
				},
				b
			);
		}

		bool operator()(const uid_sessionid_t &a,
				size_t sessionid) const noexcept
		{
			return operator()(sessionid, a);
		}
	};
}

struct uid_sessionid_to_chars {
	char buf[std::numeric_limits<uid_t>::digits10+
		std::numeric_limits<size_t>::digits10+5];

	uid_sessionid_to_chars(const uid_sessionid_t &);

	operator std::string_view() const & noexcept
	{
		return std::string_view(buf);
	}

	std::string path() const noexcept;

	operator std::string_view() && =delete;
};

#endif
