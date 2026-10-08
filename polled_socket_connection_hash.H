/*
** Copyright 2022-2026 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef polled_socket_connection_hash_H
#define polled_socket_connection_hash_H

#include <concepts>
#include <utility>
#include <sys/types.h>

// vera_socket_poller_and_connections uses a std::unordered_set of socket
// connections.
//
// The set is keyed by the connection's file descriptor. To simplify using the
// std::unordered_set, we can look up by either an int file descriptor or, for
// convenience, by the external_filedesc itself. This means we need a way to
// retrieve the file descriptor from the element type, which is what the
// following concept and specialization provide.

template<typename T>
struct get_file_descriptor {

	template<typename U>
	static int getfd(U &&u) noexcept
	{
		return u;
	}
};

template<typename T>
concept get_file_descriptor_from_fd = requires(T &ptr) {
	{ ptr.fd->fd } -> std::convertible_to<int>;
};

template<get_file_descriptor_from_fd T>
struct get_file_descriptor<T>
{
	template<typename U>
	static int getfd(U &&u) noexcept
	{
		return u.fd->fd;
	}
};

// For std::unordered_set, a transparent equality test.
struct file_descriptor_equal {
	typedef void is_transparent;

	template<typename T, typename U>
	bool operator()(T &&a,
			U &&b) const noexcept
	{
		return get_file_descriptor<std::remove_cvref_t<T>>{}.getfd(
			std::forward<T>(a)
		) ==
			get_file_descriptor<std::remove_cvref_t<U>>{}.getfd(
				std::forward<U>(b)
			);
	}
};

// For std::unordered_set, a transparent hash function.

struct file_descriptor_hash {
	typedef void is_transparent;

	template<typename T>
	size_t operator()(T &&u) const
	{
		return get_file_descriptor<std::remove_cvref_t<T>>{}.getfd(
			std::forward<T>(u)
		);
	}
};

#endif
