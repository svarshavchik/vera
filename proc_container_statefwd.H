/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef proc_container_statefwd_h
#define proc_container_statefwd_h

#include <variant>

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

#endif
