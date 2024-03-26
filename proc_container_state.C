/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "proc_container_state.H"
#include "messages.H"

static void set_scheduled(state_timeline &timeline,
			  const elapsed_time &timestamp)
{
	if (timeline.scheduled)
		return;

	timeline.scheduled=timestamp;
}

static void set_inprogress(state_timeline &timeline,
			   const elapsed_time &timestamp)
{
	if (timeline.inprogress)
		return;

	timeline.inprogress=timestamp;
}

static void set_started(state_timeline &timeline,
			const elapsed_time &timestamp)
{
	if (timeline.completed)
		return;

	timeline.completed=timestamp;
	timeline.final_label=STATE_STARTED::label.label;
}

static void set_stopped(state_timeline &timeline,
			const elapsed_time &timestamp)
{
	if (timeline.completed)
		return;

	timeline.completed=timestamp;
	timeline.final_label=STATE_STOPPED::label.label;
}

static void set_noop(state_timeline &timeline,
		     const elapsed_time &timestamp)
{
}

const STATE_LABEL STATE_START_PENDING::label{
	set_scheduled, _("start pending")};
const STATE_LABEL STATE_START_PENDING_MANUAL::label{
	set_scheduled, _("start pending (manual)")};

const STATE_LABEL STATE_STARTING::label{
	set_inprogress, _("starting")};
const STATE_LABEL STATE_STARTING_MANUAL::label{
	set_inprogress, _("starting (manual)")};

const STATE_LABEL STATE_RESPAWNING::label{
	set_noop, _("respawning")};
const STATE_LABEL STATE_RESPAWNING_MANUAL::label{
	set_noop, _("respawning (manual)")};

// NOTE: started and stopped should have the same # of characters, switchlogs
// rely on that

const STATE_LABEL STATE_STARTED::label{
	set_started, _("started")};
const STATE_LABEL STATE_STARTED_MANUAL::label{
	set_started, _("started (manual)")};

const STATE_LABEL STATE_STOP_PENDING::label{
	set_scheduled, _("stop pending")};
const STATE_LABEL STATE_STOPPING::label{
	set_inprogress, _("stopping")};

const STATE_LABEL STATE_FORCE_REMOVING::label{
	set_inprogress, _("force-removing")};
const STATE_LABEL STATE_REMOVING::label{
	set_inprogress, _("removing")};

// NOTE: started and stopped should have the same # of characters, switchlogs
// rely on that

const STATE_LABEL STATE_STOPPED::label{
	set_stopped, _("stopped")};
