/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef poller_h
#define poller_h

#include <functional>
#include <sys/types.h>
#include <variant>
#include <string>
#include <memory>
#include <filesystem>

//! A polled file descriptor

//! The file descripto gets owned and gets passed to the constructor.
//! polledfd takes ownership of it, and automatically close()s it when it
//! gets destroyed.
//!
//! The file descriptor should have O_NONBLOCK and O_CLOEXEC set, before
//! pollefd is constructed.
//!
//! The 2nd parameter to the constructor is a callable object that
//! do_poll() invokes whenever the file descriptor is readable, and it
//! receives the file descriptor as the parameter.
//!
//! This object is movable, but not copyable

class polledfd {

	int fd{-1};

	void destroy();

public:
	polledfd()=default;
	polledfd(int fd, const std::function<void (int)> &callback);

	polledfd(int fd, std::function<void (int)> &&callback);

	~polledfd();

	polledfd(polledfd &&);

	polledfd &operator=(polledfd &&);
	polledfd &operator=(const polledfd &)=delete;
};

/* Poll and take action */

void do_poll(int timeout= -1);


typedef std::function<void (const char *, uint32_t)> inotify_cb_t;

/*! An inotify-based watcher

The constructor's parameters are: a pathname, a mask (see inotify(7)
for the description of the mask value that gets passed to inotify_add_watch),
and a callback.

The callback takes two parameters, a const char *, which might be null, and
the occured event mask.

operator bool is an indication whether the watch was installed successfully.

The destructor removes the watch, and the callback.

An edge case occurs if a new watch gets constructed after other watches
get removed. The new watch does not get added immediately after but only after
the removed watch gets acknowledged by IN_IGNORED. It's possible that
at that time inotify_add_watch fails because the path does not exist. This
results in a synthesized invocation of the callback with an IN_IGNORED value.

To summarize:

1) Operator bool is false after construction when the watch failed because
the watched file or directory definitely does not exist.

2) Operator bool returning true after construction guarantees the callback
getting invoked with an IN_IGNORED mask.

3) In all cases, the callback that gets passed to the constructor does not
get called after either this object gets destroyed or the ultimate moved-to
object gets destroyed (these objects are movable-only).

 */

class inotify_watch_handler {

	int wd;
	bool installed;

public:

	class delink;

	friend class delink;

	operator bool() const { return installed; }

	inotify_watch_handler() : wd{-1}, installed(false) {}

	inotify_watch_handler(const std::string &pathname, uint32_t mask,
			      inotify_cb_t);

	~inotify_watch_handler();

	//! Value for the mask parameter to the constructor

	//! Specifies that pathname is a directory.

	static const uint32_t mask_dir;

	//! Movable object
	inotify_watch_handler(inotify_watch_handler &&);

	//! Non-assignable object.
	inotify_watch_handler &operator=(
		inotify_watch_handler &&
	);

	//! Non-assignable object.
	inotify_watch_handler &operator=(
		const inotify_watch_handler &
	)=delete;

private:
	void destroy();
};

/*! Monitor a directory for changes.

 */

class monitor_hierarchy : inotify_watch_handler {

public:
	typedef std::function<void (const char *)> changed_t;
	typedef std::function<void (std::string)> fatal_error_t;

	struct monitor_info;

	monitor_hierarchy(const std::filesystem::path &dir,
			  changed_t changed,
			  fatal_error_t fatal_error);

private:
	monitor_hierarchy(std::filesystem::path dir,
			  changed_t &changed,
			  std::shared_ptr<fatal_error_t> fatal_error);

	monitor_hierarchy(std::shared_ptr<monitor_info> info);
public:
	using inotify_watch_handler::operator bool;
};

//! Return a file descriptor for /dev/null

int devnull();

#endif
