/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/

#ifndef PRIVREQUEST_SENDFDC_H
#define PRIVREQUEST_SENDFDC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*! Transmit file descriptors over a Unix-domain socket.
 *
 * Sends 'count' file descriptors from 'fds' to 'sockfd' using
 * SCM_RIGHTS ancillary data via sendmsg(). A dummy single-byte
 * payload is sent as the data component (no meaningful data
 * transfer).
 *
 * @param sockfd  socket fd (must be a connected Unix-domain socket)
 * @param fds     array of file descriptors to transmit
 * @param count   number of file descriptors in 'fds'
 * @return        bytes sent on success (typically 1), -1 on error
 */
extern int send_fds(int sockfd, const int *fds, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* PRIVREQUEST_SENDFDC_H */
