#! /bin/sh

test ! -f /sbin/vera || exit 0      # Upgrading

oops=0

for f in /etc/rc.d/rc.sysvinit /etc/rc.d/rc.local /etc/rc.d/rc.local_shutdown \
			       /sbin/init
do
    if test -f ${f}.init
    then
	mv -f ${f}.init ${f}
	echo "Unhook ${f} by force!"
	oops=1
    fi
done

if test "$oops" = 1
then
    echo "Looks like you forgot to unhook, you're in trouble!"
    echo "Hopefully I just saved your bacon, but you MUST sync"
    echo "and do a hard power off!"
    echo ""
    echo "If you survive: rm -rf /lib/vera /etc/vera and /var/log/vera"
else
    rm -rf /lib/vera /etc/vera /var/log/vera
fi
