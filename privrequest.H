/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef privrequest_h
#define privrequest_h

#include "external_filedesc.H"
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <optional>
#include <signal.h>

// Send requests and process responses over a privileged (root) sockets.

// Send a start request
void send_start(const external_filedesc &efd, std::string name);

// Initial start attempt, did it work?

// Returns an empty string if it did, or an error message.
std::string get_start_status(const external_filedesc &efd);

// Wait for the start request to finish.
bool get_start_result(const external_filedesc &efd);

#define START_RESULT_OK "0"

// Send a stop request
void send_stop(const external_filedesc &efd, std::string name);

// Initial stop attempt, did it work?

// Returns an empty string if it did, or an error message.
std::string get_stop_status(const external_filedesc &efd);

// Wait for the stop request to finish.
void wait_stop(const external_filedesc &efd);

// Send a restart request
void send_restart(const external_filedesc &efd, std::string name);

// Initial restart attempt, did it work?

// Returns an empty string if it did, or an error message.
std::string get_restart_status(const external_filedesc &efd);

// Wait for the restart request to finish.
int wait_restart(const external_filedesc &efd);

// Send a reload request
void send_reload(const external_filedesc &efd, std::string name);

// Initial reload attempt, did it work?

// Returns an empty string if it did, or an error message.
std::string get_reload_status(const external_filedesc &efd);

// Wait for the reload request to finish.
int wait_reload(const external_filedesc &efd);

// Request a re-exec
void request_reexec(const external_filedesc &efd);

// Request a new runlevel

void request_runlevel(const external_filedesc &efd,
		      const std::string &runlevel);

std::string get_runlevel_status(const external_filedesc &efd);

// Request the current runlevel and aliases

void request_current_runlevel(const external_filedesc &efd);

// Returns the current runlevel and its aliases
//
// First string is the full runlevel name, followed by aliases (3, 4, etc...)

std::vector<std::string> get_current_runlevel(const external_filedesc &efd);

// Create a pair of sockets for the fake requests.

// This is used mostly for unit tests, but we also use this to queue up
// a synthesized request to switch to the default run level, at startup.

std::tuple<external_filedesc, external_filedesc> create_fake_request();

// Send a status request to the daemon

void request_status(const external_filedesc &efd);

// Helper function used to receive the file descriptor.

// Parameter is the connection to the requester.
//
// Receives and returns the file descriptor, verifying that it is IS_REG.

external_filedesc request_recvfd(const external_filedesc &efd);

// Handshake: daemon: request a file descriptor

void request_fd(const external_filedesc &efd);

// Handshake: requester: wait for the daemon to request a file descriptor

void request_fd_wait(const external_filedesc &efd);

// Send a file descriptor to the requesting daemon

void request_send_fd(const external_filedesc &efd, int statusfd);

struct container_state_info {
	std::string state;

	time_t timestamp=0;

	bool enabled=false;

	// Bare-bounes information about a process in the container.
	struct pid_info {

		// Its parent process id
		pid_t ppid{1};

		// Its command line
		std::vector<std::string> cmdline;
		bool operator==(const pid_info &) const=default;
		bool operator<=>(const pid_info &) const=default;
	};

	struct hier_pid_info;

	// Processes arranged in a parent-child hierarchy
	typedef std::map<pid_t, hier_pid_info> hier_pids;

	struct hier_pid_info {

		// This is actually this process's pid. Thsi is the parent
		// of all the child_pids
		pid_info parent_pid;

		// parent_pid's child processes.
		hier_pids child_pids;

		bool operator==(const hier_pid_info &) const=default;
		bool operator<=>(const hier_pid_info &) const=default;
	};

	// Processes in this container/
	hier_pids processes;

	bool operator==(const container_state_info &) const=default;
};

// Receive and parse the container state.

// This is the end result of the following sequence: request_status, followed
// by request_fd_wait, followed by request_send_fd(). get_status() gets
// called with the daemon connection and the temporary file descriptor that
// was passed to request_sendfd().
//
// Returns a map, the key is the container name.
//
// container_state_info does not reflect overrides.

std::unordered_map<std::string, container_state_info> get_status(
	const external_filedesc &efd,
	int statusfd
);

// Update the container_state_info map to reflect the current overrides

void update_status_overrides(
	std::unordered_map<std::string, container_state_info> &status,
	const std::string &globaldir,
	const std::string &localdir,
	const std::string &overridedir);


#endif
