#! /bin/sh

set -e;
/sbin/vlad inittab
/sbin/vlad rehook || :
if test -f /sbin/init.init
then
    if /sbin/vlad vera-up
    then
	/sbin/vlad reexec
    fi
else
    echo "Now run \"vlad hook\" and reboot."
    echo ""
    echo "IMPORTANT: run \"vlad unhook\", reboot, then \"vlad unhook\" again"
    echo "before removing vera with removepkg, or before updating sysvinit"
    echo "or logrotate."
fi
