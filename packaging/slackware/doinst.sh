#! /bin/sh

set -e;
/sbin/vlad inittab
echo "Now run \"vlad hook\" and reboot."
echo ""
echo "IMPORTANT: run \"vlad unhook\", reboot, then \"vlad unhook\" again"
echo "before removing vera with removepkg, or before updating vera or sysvinit"
echo "packages."
