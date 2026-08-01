/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/

#include "pam_vera.C"

int main()
{
        (void)pam_sm_close_session(nullptr, 0, 0, nullptr);

        return 0;
}