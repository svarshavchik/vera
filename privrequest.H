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
#include <optional>

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

// Read the status request from the daemon, save it in a temporary file,
// send the temporary file descriptor to the requesting connection.

void proxy_status(const external_filedesc &privfd,
		  const external_filedesc &requestfd);

struct container_state_info {
	std::string state;

	bool operator==(const container_state_info &) const=default;
};

// Receive and parse the container state.

// Returns a map, the key is the container name. nullopt return indicates an
// error.

std::optional<std::unordered_map<std::string, container_state_info>> get_status(
	const external_filedesc &requestfd
);

#endif
