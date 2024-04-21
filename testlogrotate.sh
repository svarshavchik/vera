#! /bin/sh

set -e

rm -rf testlogrotate.d
mkdir testlogrotate.d

cat >testlogrotate.d/logrotate.conf <<EOF
include `pwd`/testlogrotate.d/logrotate.d
EOF

mkdir testlogrotate.d/logrotate.d
cat >testlogrotate.d/logrotate.d/ntp <<EOF
`pwd`/testlogrotate.d/rc.d/ntp restart
`pwd`/testlogrotate.d/rc.d/ldap restart
EOF

mkdir testlogrotate.d/rc.M
>testlogrotate.d/rc.M/ntp

$srcdir/vera-logrotate-migrate \
	 `pwd`/testlogrotate.d/logrotate.conf \
	 `pwd`/testlogrotate.d/logrotate.conf.vera \
	 `pwd`/testlogrotate.d/logrotate.d \
	 `pwd`/testlogrotate.d/logrotate.d.vera \
	 `pwd`/testlogrotate.d/rc.d \
	 `pwd`/testlogrotate.d/rc.M

>testlogrotate.d/logrotate.d.vera/xxxx

$srcdir/vera-logrotate-migrate \
	 `pwd`/testlogrotate.d/logrotate.conf \
	 `pwd`/testlogrotate.d/logrotate.conf.vera \
	 `pwd`/testlogrotate.d/logrotate.d \
	 `pwd`/testlogrotate.d/logrotate.d.vera \
	 `pwd`/testlogrotate.d/rc.d \
	 `pwd`/testlogrotate.d/rc.M

echo "logrotate.conf.vera:"                  >testlogrotate.d.out
cat testlogrotate.d/logrotate.conf.vera     >>testlogrotate.d.out
echo "logrotate.d.vera:"                    >>testlogrotate.d.out
ls testlogrotate.d/logrotate.d.vera         >>testlogrotate.d.out
echo "logrotate.d.vera/ntp:"                >>testlogrotate.d.out
cat testlogrotate.d/logrotate.d.vera/ntp    >>testlogrotate.d.out

cat >testlogrotate.d.expected <<EOF
logrotate.conf.vera:
include `pwd`/testlogrotate.d/logrotate.d.vera
logrotate.d.vera:
ntp
logrotate.d.vera/ntp:
vlad restart system/rc.M/ntp
`pwd`/testlogrotate.d/rc.d/ldap restart
EOF

diff -U 3 testlogrotate.d.expected testlogrotate.d.out
rm -rf testlogrotate.d.expected testlogrotate.d.out testlogrotate.d
