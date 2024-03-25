/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef proc_container_statefwd_h
#define proc_container_statefwd_h

#include <variant>
#include <string>
#include <tuple>
#include <optional>
#include <compare>

struct state_stopped;
struct state_starting;
struct state_started;
struct state_stopping;

//! The state of a process container

//! - \c state_stopped - the container is stopped
//!
//! - \c state_starting - the container is starting
//!
//! - \c state_started - the container has started
//!
//! - \c state_stopping - the container is stopping

typedef std::variant<state_stopped,
		     state_starting,
		     state_started,
		     state_stopping> proc_container_state;


//! How long each container took to start or stop, as logged

struct elapsed_time {
	unsigned seconds{0};
	unsigned milliseconds{0};

	//! Calculate the difference in time. We expect time to be monotonical.

	elapsed_time operator-(const elapsed_time &o) const
	{
		elapsed_time diff;

		if (seconds > o.seconds ||
		    (seconds == o.seconds && milliseconds > o.milliseconds))
		{
			diff.seconds=seconds - o.seconds;

			auto ms_diff = milliseconds;

			if (ms_diff < o.milliseconds)
			{
				ms_diff += 1000;
				diff.seconds--;
			}
			diff.milliseconds=ms_diff-o.milliseconds;
		}

		return diff;
	}

	bool operator==(const elapsed_time &o) const
	{
		return seconds == o.seconds && milliseconds == o.milliseconds;
	}

	auto operator<=>(const elapsed_time &o) const
	{
		if (seconds < o.seconds)
		{
			return std::strong_ordering::less;
		}
		else if (seconds == o.seconds)
		{
			if (milliseconds == o.milliseconds)
				return std::strong_ordering::equal;

			if (milliseconds < o.milliseconds)
				return std::strong_ordering::less;
		}
		return std::strong_ordering::greater;
	}
};

/*! Create a timeline from switchlog entries

When the switch log gets read back it gets analyzed, and the read entries
are reduced in scope to:

1) when each container was scheduled for starting or stopping

2) when each container began starting or stopping

3) when each container started or stopped

 */

struct state_timeline {

	std::optional<elapsed_time> scheduled, inprogress, completed;

	const char *final_label{nullptr};
};

//! A human-readable label
struct STATE_LABEL {

	//! Update the timeline, when this record is read from the switchlog

	void (*const update_timeline)(state_timeline &timeline,
				      const elapsed_time &timestamp);

	//! The actual label
	const char * const label;

	//! Used in unit test, for convenience diagnostics
	std::string label_str() const { return label; }
};

//! A container state label

struct STATE_START_PENDING {
	static const STATE_LABEL label; //!< The label
};

//! A container state label

struct STATE_START_PENDING_MANUAL {
	static const STATE_LABEL label; //!< The label
};

//! A container state label

struct STATE_STARTING {
	static const STATE_LABEL label; //!< The label
};

//! A container state label

struct STATE_STARTING_MANUAL {
	static const STATE_LABEL label; //!< The label
};

//! A container state label

struct STATE_RESPAWNING {
	static const STATE_LABEL label; //!< The label
};

//! A container state label

struct STATE_RESPAWNING_MANUAL {
	static const STATE_LABEL label; //!< The label
};

//! A container state label

struct STATE_STARTED {
	static const STATE_LABEL label; //!< The label
};

//! A container state label

struct STATE_STARTED_MANUAL {
	static const STATE_LABEL label; //!< The label
};

//! A container state label

struct STATE_STOP_PENDING {
	static const STATE_LABEL label; //!< The label
};

//! A container state label

struct STATE_STOPPING {
	static const STATE_LABEL label; //!< The label
};

//! A container state label

struct STATE_FORCE_REMOVING {
	static const STATE_LABEL label; //!< The label
};

//! A container state label

struct STATE_REMOVING {
	static const STATE_LABEL label; //!< The label
};

//! A container state label

struct STATE_STOPPED {
	static const STATE_LABEL label; //!< The label
};

/*! All container state labels

All types defined above must be included in this tuple
*/

typedef std::tuple<STATE_START_PENDING,
		   STATE_START_PENDING_MANUAL,
		   STATE_STARTING,
		   STATE_STARTING_MANUAL,
		   STATE_STARTED,
		   STATE_STARTED_MANUAL,
		   STATE_RESPAWNING,
		   STATE_RESPAWNING_MANUAL,
		   STATE_STOP_PENDING,
		   STATE_STOPPING,
		   STATE_FORCE_REMOVING,
		   STATE_REMOVING,
		   STATE_STOPPED> ALL_STATE_LABELS;

#endif
