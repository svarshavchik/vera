/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef privrequest_h
#define privrequest_h

#include "external_filedesc.H"
#include "proc_loaderfwd.H"
#include <algorithm>
#include <sys/stat.h>
#include <sys/socket.h>
#include <string>
#include <array>
#include <vector>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <time.h>

std::string environconfig();

/*! Bare-bones information about a process in the container

Represents information about a single process
within a container.

*/

struct pid_info {

	// Its parent process id
	pid_t ppid{1};

	// exe's device/inode

	dev_t exedev{};
	ino_t exeino{};

	// Its command line
	std::vector<std::string> cmdline;
	bool operator==(const pid_info &) const=default;
	std::strong_ordering operator<=>(const pid_info &) const=default;
};

struct hier_pid_info {

	// This is actually this process's pid. This is the parent
	// of all the child_pids
	pid_info parent_pid;

	// Processes arranged in a parent-child hierarchy
	using hier_pids=std::map<pid_t, hier_pid_info>;

	// parent_pid's child processes.
	hier_pids child_pids;

	bool operator==(const hier_pid_info &) const;
	std::strong_ordering operator<=>(
		const hier_pid_info &
	) const;
};

// Send requests and process responses over a privileged (root) sockets.

// Send a start request
[[nodiscard]] bool send_start(const external_filedesc &efd, std::string name);

// Initial start attempt, did it work?

// Returns an empty string if it did, or an error message.
std::string get_start_status(const external_filedesc &efd);

// Wait for the start request to finish.
bool get_start_result(const external_filedesc &efd);

#define START_RESULT_OK "0"

// Send a stop request
[[nodiscard]] bool send_stop(const external_filedesc &efd, std::string name);

// Initial stop attempt, did it work?

// Returns an empty string if it did, or an error message.
std::string get_stop_status(const external_filedesc &efd);

// Wait for the stop request to finish.
void wait_stop(const external_filedesc &efd);

// Send a restart request
[[nodiscard]] bool send_restart(const external_filedesc &efd, std::string name);

// Initial restart attempt, did it work?

// Returns an empty string if it did, or an error message.
std::string get_restart_status(const external_filedesc &efd);

// Wait for the restart request to finish.
int wait_restart(const external_filedesc &efd);

// Send a reload request
[[nodiscard]] bool send_reload(const external_filedesc &efd, std::string name);

// Initial reload attempt, did it work?

// Returns an empty string if it did, or an error message.
std::string get_reload_status(const external_filedesc &efd);

// Wait for the reload request to finish.
int wait_reload(const external_filedesc &efd);

// Request vera to terminate and replace itself with another process.
// This is used to execute rc.0 or rc.6, which ends in a reboot.

[[nodiscard]] bool send_sysdown(const external_filedesc &efd,
		  std::string runlevel,
		  std::string command);

// Acknowledge that system down is in progress.

std::string get_sysdown_status(const external_filedesc &efd);

// Request a re-exec
[[nodiscard]] bool request_reexec(const external_filedesc &efd);

// Request a new runlevel

[[nodiscard]] bool request_runlevel(const external_filedesc &efd,
		      const std::string &runlevel);

// Wait for the runlevel request to be validated.

// Returns an empty string if the request is processing, or an error message.

std::string get_runlevel_status(const external_filedesc &efd);

// Wait for the new runlevel to come up.
int wait_runlevel(const external_filedesc &efd);

// Request the current runlevel and aliases

[[nodiscard]] bool request_current_runlevel(const external_filedesc &efd);

// Returns the current runlevel and its aliases
//
// First string is the full runlevel name, followed by aliases (3, 4, etc...)

std::vector<std::string> get_current_runlevel(const external_filedesc &efd);

/*! Create sockets for status request.

  This is used mostly for unit tests, but we also use this to queue up
  a synthesized request to switch to the default run level, at startup.

  Additionally we create a memfd file to hold a fake command and return a
  vector containing one external_file_desc, the memfd file.
*/

std::tuple<external_filedesc, external_filedesc,
	std::vector<external_filedesc>> create_fake_request();

// Send a status request to the daemon
//
// efd and fds are returned from a successful try_connect_vera_priv()
//
// Returns true if the request was sent, false otherwise.
//
// try_connect_vera_priv() is defined in hook.H and hook.C, and it returns
// (if successful)  the connection to the vera daemon, and a vector containing
// one external_filedesc, which is a temporary file descriptor into which the
// command to the daemon gets written. request_status() writes the status
// request, and creates a 2nd temp filedesc that gets added to fds. This can
// now be sent to the daemon.

bool request_status(
	const external_filedesc &efd,
	std::vector<external_filedesc> &fds
);

/*! Session start request */

[[nodiscard]] bool request_session_start(
	const external_filedesc &efd,
	const std::string &name
);

/*! What receive_fd() returns

  It returns a list of file descriptors received over the socket, and,
  for the public socket (which has credentials enabled) the sender's
  credentials.
*/

typedef std::tuple<std::vector<external_filedesc>,
		   std::array<char,32> > receive_fds_ret_t;

/*! Helper function used to receive the file descriptors over a socket.

  Parameter is the connection to the requester.

  Receives and returns the received file descriptors.
*/

receive_fds_ret_t receive_fds(const external_filedesc &efd);

/*! Send file descriptors to the vera daemon

  efd is the connection to the daemon (requesting it to ead the file
  descriptors).

  fds is the vector of file descriptors to send. The first file descriptor is
  a memfd file descriptor containing the requested command. A status command
  also uses a second memfd file descriptor into which the daemon writes the
  status. Start, stop, restart, and reload commands optionally pass in an
  additional file descriptor that's receives the redirected standard output
  and error from the commands executed as a result of the request.

  The daemon replies when the command is processed. Typically an empty line
  indicates a success, otherwise an error message. Success means that for
  the status command the status has been written to the second file
  descriptor.
*/

[[nodiscard]] bool request_send_fd(
	const external_filedesc &efd,
	const std::vector<external_filedesc> &fds
);

[[nodiscard]] bool request_send_fd(
	const external_filedesc &efd,
	const std::vector<int> &fds
);

// Create a socket pair for vera requests

std::tuple<external_filedesc, external_filedesc> create_vera_socketpair();

struct container_state_info {
	std::string state;
	proc_override::resources_t resources;

	std::string elapsed;

	time_t timestamp=0;

	bool enabled=false;

	// Processes in this container.
	hier_pid_info::hier_pids processes;

	// The dependencies
	std::unordered_set<std::string> dep_requires, dep_requires_first,
		dep_required_by, dep_starting_first, dep_stopping_first;


	bool operator==(const container_state_info &) const;
};

// Implement default comparison operators for hier_pid_info

// We must manually compare the class members due to the
// nested templated types.

inline bool hier_pid_info::operator==(
	const hier_pid_info &other
) const=default;

inline std::strong_ordering hier_pid_info::operator<=>(
	const hier_pid_info &other
) const
{
	std::strong_ordering ret=parent_pid<=>other.parent_pid;
	if (ret!=std::strong_ordering::equivalent)
		return ret;

	return std::lexicographical_compare_three_way(
		child_pids.begin(), child_pids.end(),
		other.child_pids.begin(), other.child_pids.end()
	);
}

// Implement default comparison operator for container_state_info

inline bool container_state_info::operator==(
	const container_state_info &other
) const=default;

/*! Receive and parse the container state.

  The sequence of events was request_status() followed by request_send_fd().
  get_status() waits for the response from the daemon, at which point the
  statusfd has the daemon status, which get_status() parses and returns.

  Returns a map, the key is the container name.

  container_state_info does not reflect overrides.
*/

std::unordered_map<std::string, container_state_info> get_status(
	const external_filedesc &efd,
	const std::vector<external_filedesc> &cmdfds
);

/*! "/proc/"

  Used to read process information. Overridden in unit tests.
*/

extern const char slashprocslash[];

/*! Populate pid_info

This reads the pids in the container, then reads /proc and populates
a pid_info map.

 */

void get_pid_status(const std::string &container_name,
		    std::unordered_map<pid_t, pid_info> &processes);

/*! Sort container pids into a hierarchical tree

This removes each pid from the map populated by get_pid_status and puts them
into a hierarchical parent/children tree.

 */
void sort_pids(std::unordered_map<pid_t, pid_info> &processes,
	       hier_pid_info::hier_pids &pids);

// Update the container_state_info map to reflect the current overrides

void update_status_overrides(
	std::unordered_map<std::string, container_state_info> &status,
	const std::string &globaldir,
	const std::string &localdir,
	const std::string &overridedir);

// Send environment variable set

[[nodiscard]] bool send_setenv(const external_filedesc &fd,
			     std::string name,
			     std::string value);

// Send environment variable unset

[[nodiscard]] bool send_unsetenv(const external_filedesc &fd,
				 std::string name);

// Wait for the setenv or unsetenv to be processed
//
// Returns: 0 - success, 1 - error.

int wait_setunsetenv(const external_filedesc &fd);

// Request a freeze

[[nodiscard]] bool request_freeze(const external_filedesc &efd,
				    const std::string &name);

// Request a thaw

[[nodiscard]] bool request_thaw(const external_filedesc &efd,
				  const std::string &name);

// Status of the thaw command

std::string get_freeze_thaw_status(const external_filedesc &efd);

/*! Create a fork request to the session manager

Takes the argv of the command to run and the environment variables and
creates a request to the session manager to fork accordingly.

pidpipe: when the child has been forked, the pid is written to this pipe.

releasepipe: after the pid is written to this pipe, the child process
reads this pipe, then proceeds to try to exec the command, and then
the pidpipe is either closed, or a forkstatus is written to it indicating
the reason for the failure. vera reads the pid, registers the pid into

outerrpipe is a socket that gets connected to standard output and error of
the forked process.

*/

[[nodiscard]] bool request_fork(
	const external_filedesc &efd,
	int pidpipe,
	int releasepipe,
	int outerrpipe,
	const std::vector<std::vector<char>> &argv,
	const std::unordered_map<std::string, std::string> &environconfigvars
);

/*! Session manager waits for a fork request, and reads it

Returns the argv vector, the environment config map, the pidpipe and
releasepipe from the request.

*/

std::tuple<std::vector<std::vector<char>>,
	std::unordered_map<std::string, std::string>,
	external_filedesc, external_filedesc, external_filedesc> receive_fork(
	const external_filedesc &efd
);

/*! Receive and execute the fork in the session manager.

Calls receive_fork(). Returns false if the socket is closed. If a fork
request is received, invokes the execvp_after_forking with the arguments
for execve() in the child process and the original parent process returns
true.

*/

bool request_and_execute_fork(const external_filedesc &efd);

/*! Executes execve() in the child process.

 */

void execvp_after_forking(
	const char *path,
	char **argv_ptrs,
	char **environ_ptrs
);

/*! Eventual outcome of a forked child process.

If the child process fails to exec the command it writes this on a pipe to
the parent process so that it knows what happened to it. If the child process
successfully execed, the socket's close-on-exec bit results in nothing to read
in the parent process, which is how it knows that the child process was
successfully forked and execed. The notregistered member indicates whether
the child process was registered in the current_container, or not. If it
could not be, the child process does not exec the program. Otherwise,
failed_errno reports the value of errno after execve fails.

*/
struct fork_status {
	int failed_errno=0;
	bool notregistered=true;
};

#endif
