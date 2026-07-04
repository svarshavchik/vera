#! /bin/sh

set -e

# Set a capability on the executable so it gets run in secure mode. The
# purported cap_net_bind_service capability itself is immaterial; what
# matters is that the executable runs in secure mode.

setcap cap_net_bind_service+ep /sbin/vlad || :

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
