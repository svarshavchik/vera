/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef privrequest_h
#define privrequest_h

#include "external_filedesc.H"
#include "proc_loaderfwd.H"
#include <algorithm>
#include <optional>
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
#include <optional>

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

/*! Wait for the start request to finish.

Returns the result of the start command. WEXITSTATUS(returned) is 0 on success.

*/

int get_start_result(const external_filedesc &efd);

#define START_RESULT_OK "0"

// Send a stop request
[[nodiscard]] bool send_stop(const external_filedesc &efd, std::string name);

// Initial stop attempt, did it work?

// Returns an empty string if it did, or an error message.
std::string get_stop_status(const external_filedesc &efd);

// Wait for the stop request to finish. Always returns 0.
int wait_stop(const external_filedesc &efd);

// Send a stop-all request

// The name parameter is ignored. The command sent is always "stop-all"
// regardless of the unit.

[[nodiscard]] bool send_stop_all(const external_filedesc &efd, std::string name);

// Initial stop-all attempt, did it work?

// Returns an empty string if it did, or an error message.
std::string get_stop_all_status(const external_filedesc &efd);

// Wait for the stop-all request to finish. Always returns 0.
int wait_stop_all(const external_filedesc &efd);

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

// Request a new runlevel

[[nodiscard]] bool request_runlevel(const external_filedesc &efd,
		      const std::string &runlevel);

// Wait for the runlevel request to be validated.

// Returns an empty string if the request is processing, or an error message.

std::string get_runlevel_status(const external_filedesc &efd);

// Wait for the new runlevel to come up.
int wait_runlevel(const external_filedesc &efd);

// Call request_runlevel, get_runlevel_status, and optionally wait_runlevel.
std::string privrequest_switch_runlevel(
		const std::string &runlevel,
		bool waitrunlevel_flag
        );

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



/*! Session start request */

[[nodiscard]] bool request_session_start(
	const external_filedesc &efd
);

/*! Start the session manager

Connects to vera, uses request_session_start() to send a session-start
request. If a green light is given, runs vera in session manager mode.

Meanwhile, returns an error message if session-start failed, otherwise an
empty string.

*/
[[nodiscard]] std::string session_start(std::string session);

/*! What receive_fd() returns

  It returns a list of file descriptors received over the socket. If no file
  descriptors are received, it reads a small amount of bytes, which are saved
  in a small char buffer. The data is always 0-terminated.

  An error or an end-of-file condition results in nullopt being returned.
*/

typedef std::optional<std::tuple<std::vector<external_filedesc>,
		   std::array<char,32> >> receive_fds_ret_t;

/*! Helper function used to receive the file descriptors over a socket.

  Parameter is the connection to the requester.

  Receives and returns the received file descriptors.
*/

receive_fds_ret_t receive_fds(const external_filedesc &efd);
receive_fds_ret_t receive_fds(int fd);

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

//! Call request_send_fd, if it fails, exit(1)

void wrap_request_send_fd(
	const external_filedesc &efd,
	const std::vector<external_filedesc> &fds
);

/*! Create a socket pair for vera requests

  Creates a socket pair and returns the two ends as external_filedesc.
  The socket is created as a local socket, and the close-on-exec bit is
  set.

  Returns nullopt if the socket pair cannot be created.
*/

std::optional<
	std::tuple<external_filedesc, external_filedesc>
> create_vera_socketpair();

/*! Set up file descriptors for a start/stop/reload/restart command.

  Creates a tmpfile and optionally a socket pair for stdout redirection.
  Returns nullopt if the socket pair cannot be created.

*/
std::optional<
	std::tuple<std::vector<external_filedesc>, external_filedesc>
>
setup_cmd(bool nowait);

struct container_state_info {
	std::string state;
	proc_override::resources_t resources;

	std::string elapsed;

	time_t timestamp=0;

	bool enabled=false;

	// Processes in this container.
	hier_pid_info::hier_pids processes;

	// The dependencies
	std::unordered_set<std::string> dep_requires,
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


/*! Get and parse container status in one go.

Sends a status request to the vera daemon via connect_vera_priv()
(which returns the connection efd and a memfd containing the command).
Writes the status request and creates a 2nd memfd into which the daemon
writes its response. It then receives and parses the container state from
the daemon's status fd, returning a map where the key is the container name.
Returns std::nullopt if request fails.

container_state_info does not reflect overrides.

*/
std::optional<std::unordered_map<std::string,
	container_state_info>> do_status(bool session);

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

/*! Initiate a freeze for a container and wait for it to complete.

Connects to the vera daemon, sends a freeze request, and returns the
status. Returns an empty string on success or an error message on failure.

*/
[[nodiscard]] std::string do_freeze(const std::string &name);

/*! Initiate a thaw for a container and wait for it to complete.

Connects to the vera daemon, sends a thaw request, and returns the
status. Returns an empty string on success or an error message on failure.

*/
[[nodiscard]] std::string do_thaw(const std::string &name);

/*! Executes execvp() in the child process.

 */

void execvp_after_forking(
	const char *path,
	char **argv_ptrs
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

/* Prepare the argv vector for execve(), and initialize the environment.

   argv_vec: A vector of vectors of chars, where each vector of chars is one
   argument for execve(). A \0 byte is appended to each char vector, this
   parameter is modified thusly.

   argv_ptrs: A vector of chars pointers. This array is initialize to point to
   the first character of each char vector in argv_vec, followed by a null ptr.
   The caller passes this directly to execvep.

   environ_map: A map of environment variables. This map is used to call
   setenv() to initialize the environment variables.

   Afterwards, privrequest_execve_prep_pg() gets called, before returning.
*/

void privrequest_execve_prep_args(
	std::vector<std::vector<char>> &argv_vec,
	std::vector<char *> &argv_ptrs,
	const std::unordered_map<std::string, std::string> &environ_map
);

/* Prepare the process group in the child process.

   This function should be called in the child process after forking, and
   before execve(). It will create a new process group and set the
   process group leader.
*/

void privrequest_execve_prep_pg();

/*! The start command

Uses do_ssrr() to process a "start" command.

*/
std::string do_start(bool session, std::string unit, bool nowait);

/*! The stop command

Uses do_ssrr() to process a "stop" command.

*/
std::string do_stop(bool session, std::string unit, bool nowait);

/*! The restart command

Uses do_ssrr() to process a "restart" command.

*/
std::string do_restart(bool session, std::string unit, bool nowait);

/*! The reload command

Uses do_ssrr() to process a "reload" command.

*/
std::string do_reload(bool session, std::string unit, bool nowait);

/*! The stop-all command

Uses do_ssrr() to process a "stop-all" command for all session containers.
*/
std::string do_stop_all(bool nowait);

/*! Atomically update binary symlinks for vera/vlad.

    For each service in {vera, vlad}, checks that the "-inst" hard link
    (e.g. /sbin/vera-inst) exists and is equivalent to the binary path
    (e.g. /sbin/vera). If a stale link is found, removes the temporary
    and replaces it, atomically via a rename. All filesystem operations
    use std::error_code to avoid exceptions.

    */
void update_binlinks(std::string_view sbindir);

#endif
