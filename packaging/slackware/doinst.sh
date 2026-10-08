#! /bin/sh

set -e

# Initial install links vlad-inst and vera-inst to vlad and vera
# Do not clobber the existing ones, on an upgrade path.
ln /sbin/vlad-inst /sbin/vlad 2>/dev/null || true
ln /sbin/vera-inst /sbin/vera 2>/dev/null || true

# Reimport initscript. If the new version hooks any files the previous
# version didn't hook, and if vera is already hooked in, hook the new files.
/sbin/vlad inittab
/sbin/vlad rehook || :

# Important instructions for a new install
if test ! -f /sbin/init.init
then
    echo "Now run \"vlad hook\" and reboot."
    echo ""
    echo "IMPORTANT: run \"vlad unhook\", reboot, then \"vlad unhook\" again"
    echo "before removing vera with removepkg, or before updating sysvinit"
    echo "or logrotate."
fi

# Install pam_vera.so into pam.d.

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
        # Must be already installed, don't change
	    rm -f "$pamdir/$file.new"
	    continue;
    fi

    echo "Saving original as $pamdir/$file.orig"
    cp "$pamdir/$file" "$pamdir/$file.orig"
    echo "Installing $pamdir/$file"
    mv "$pamdir/$file.new" "$pamdir/$file"
done
