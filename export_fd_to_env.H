/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/

#ifndef export_fd_to_env_H
#define export_fd_to_env_H

#include "external_filedesc.H"

/* Export a file descriptor number to the environment under the given variable
   name. This is done before exec-ing another process, so it knows where to
   find the file descriptor. The current external_filedesc is expected to
   have close-on-exec bit set, so we dup this file descriptor and export
   the duped file descriptor.

   Returns false if dup() or setenv() fails.
*/

bool export_fd_to_env(const external_filedesc &fd, const char *varname);

#endif
