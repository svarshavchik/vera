#! /bin/sh

set -e

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

pamdir=/etc/pam.d

for file in `ls $pamdir 2>/dev/null`
do
    case "$file" in
	*~)
	    continue
	    ;;
	*.*)
	    continue
	    ;;
	*)
	    if egrep -q '\bpam_vera\.so\b' "$pamdir/$file"
	    then
		continue
	    fi
	    if egrep -q '\bpam_loginuid\.so\b' "$pamdir/$file"
	    then
		:
	    else
		continue
	    fi
    esac

    pattern="/^[[:space:]]*session[[:space:]]([[:space:]]*)[^[:space:]]+([[:space:]]+)pam_loginuid\.so([[:space:]]|$)/"

    awk '
{
    print
    if (match($0, '"$pattern"', m)) {
        ws1=m[1]
        if (ws1 == "")
            ws1 = " "
        ws2=m[2]
        print "-session" ws1 "optional" ws2 "pam_vera.so"
    }
}
' "$pamdir/$file" >"$pamdir/$file.new"

    if cmp -s "$pamdir/$file" "$pamdir/$file.new"
    then
	rm -f "$pamdir/$file.new"
	continue;
    fi

    echo "Saving original as $pamdir/$file.orig"
    cp "$pamdir/$file" "$pamdir/$file.orig"
    echo "Installing $pamdir/$file"
    mv "$pamdir/$file.new" "$pamdir/$file"
done
