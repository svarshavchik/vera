#define PAM_SM_SESSION
#include <security/pam_modules.h>
#include <security/pam_ext.h>
#include <syslog.h>

#include "privrequest.H"

extern "C" {

PAM_EXTERN int pam_sm_open_session(
	pam_handle_t *pamh,
	int flags,
	int argc,
	const char **argv
)
{
    return PAM_SUCCESS;
}

PAM_EXTERN int pam_sm_close_session(
	pam_handle_t *pamh,
	int flags,
	int argc,
	const char **argv
)
{
	const char *user = nullptr;
	if (pam_get_user(pamh, &user, nullptr) != PAM_SUCCESS || user == nullptr) {
	user = "unknown";
	}

	pam_syslog(pamh, LOG_INFO, "Vera session shutdown for user: %s", user);

	if (process_session_stop())
		return PAM_SUCCESS;
	else
		return PAM_SESSION_ERR;
}

} // extern "C"
