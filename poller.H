/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef poller_h
#define poller_h

#include <functional>
#include <sys/types.h>
#include <string>
#include <memory>
#include <filesystem>

/*! File descriptor poller

This is called when the file descriptor is available for reading. The
function receives two parameters, the file descriptor, and an error indication.

It is up to the callback to ensure that the error indication always results
in the poller being removed, otherwise the poller gets called repeatedly.
*/

typedef std::function<void (int, bool)> poller_cb_t;

/*! Container for all active file descriptor pollers.

  It is keyed by the file descriptor, and the value is the callback
  to invoke when the file descriptor becomes readable. It is possible for
  multiple pollers to be registered for the same file descriptor, when a
  poller gets moved from one polling source to another, so this needs to
  be a multimap. By the time we try polling the move should be finished and
  we should have only one entry for each file descriptor.

*/
typedef std::unordered_multimap<int,
				poller_cb_t> poll_callbacks_t;

//! A polled file descriptor

//! The file descriptor should have O_NONBLOCK and O_CLOEXEC set, before
//! polledfd is constructed.
//!
//! The 2nd parameter to the constructor is a callable object that
//! do_poll() invokes whenever the file descriptor is readable, and it
//! receives the file descriptor as the parameter.
//!
//! This object is movable, but not copyable

class polledfd {

	int fd{-1};

	poll_callbacks_t::iterator my_cb_iter;

	void destroy();

public:
	polledfd()=default;
	polledfd(int fd, const poller_cb_t &callback);

	polledfd(int fd, poller_cb_t &&callback);

	~polledfd();

	polledfd(polledfd &&);

	polledfd &operator=(polledfd &&);
	polledfd &operator=(const polledfd &)=delete;
};

/* Poll and take action */

void do_poll(int timeout= -1);

/* Update current_time, in current_containers_infoObj */
void update_current_time();

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

	//! Value for the mask parameter to the constructor

	//! Specifies that pathname is a file whose changes we want to modify.

	static const uint32_t mask_filemodify;

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

/*! Reset the inotify poller for testing purposes.

  This should not normally be called. It is only used by the unit
  test framework to reset the inotify poller to a known state.

  What happened up to now is that various objects that were polling
  for inotify events were destroyed, queing up pending inotify events. They
  are still pending. The simplest thing to do would be to close the inotify
  file descriptor and open a new one.

*/
void test_inotify_reset();

struct poller_transferblockObj {};

typedef std::shared_ptr<poller_transferblockObj> poller_transferblock;

poller_transferblock get_poller_transferblock();

/*! Whether the poller is in a state that will survive a re-exec.

We won't re-exec when there are pending removes or deletes. There's
nothing inherent that actually prevents it. The re-exec-ed process
will rewatch everything, this is just an extra safety measure.

 */

bool poller_is_quiescent();

/*! Monitor a directory for changes.

The constructor takes two callbacks:

- Invoked with the name of the changed file in the given hierarchy.

- Invoked to report a fatal error of some sort.

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
