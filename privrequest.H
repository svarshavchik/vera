/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef privrequest_h
#define privrequest_h

#include "external_filedesc.H"
#include <string>

// Send requests and process responses over a privileged (root) sockets.

void send_start(const external_filedesc &efd, std::string name);

// Initial start attempt, did it work?

// Returns an empty string if it did, or an error message.
std::string get_start_status(const external_filedesc &efd);

// Wait for the start request to finish.
bool get_start_result(const external_filedesc &efd);

#define START_RESULT_OK "0"

#endif
