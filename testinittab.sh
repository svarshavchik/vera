set -xe
export LC_ALL=C
umask 022
srcdir="${srcdir-.}"

rm -rf testinittab.dir
mkdir -p testinittab.dir/vera/system
mkdir -p testinittab.dir/vera/rc
mkdir -p testinittab.dir/local
mkdir -p testinittab.dir/override
mkdir -p testinittab.dir/etc/rc.d/rc1.d
mkdir -p testinittab.dir/etc/rc.d/rc3.d
mkdir -p testinittab.dir/etc/rc.d/rc4.d
mkdir -p testinittab.dir/etc/rc.d/rc0.d
mkdir -p testinittab.dir/etc/rc.d/rc6.d

mkdir -p testinittab.dir/share
$VALGRIND ./testprocloader genrunlevels testinittab.dir/runlevels testinittab.dir/vera/system environconfigfile

rm environconfigfile

cat >testinittab.dir/inittab <<EOF
id:3:initdefault:
bo1::boot:bootcmd1
bo2::bootwait:bootcmd
a1:a:ondemand:ondemandcmd1
a2:a:ondemandwait:ondemandcmd2
si:S:sysinit:/etc/rc.d/rc.S
su:1S:wait:/etc/rc.d/rc.K
rc:2345:wait:/etc/rc.d/rc.M
ca::ctrlaltdel:/sbin/shutdown -t5 -r now
kb::kbrequest:kbrequestcmd
l0:0:wait:/etc/rc.d/rc.0
l6:6:wait:/etc/rc.d/rc.6
pf::powerfail:/sbin/genpowerfail start
pfn::powerfailnow:/sbin/genpowerfail start
p1::powerokwait:/sbin/genpowerfail stop
p2::powerok:/sbin/genpowerfail stop
c1:12345:respawn:/sbin/agetty 38400 tty1 linux
c2:12345:respawn:/sbin/agetty 38400 tty2 linux
x1:4:respawn:/etc/rc.d/rc.4
EOF

cat >testinittab.dir/etc/rc.d/rc.M <<EOF
-x /etc/rc.d/rc.httpd

/etc/rc.d/rc.httpd start
EOF
cat >testinittab.dir/etc/rc.d/rc.httpd <<EOF
 restart)
 'reload')
EOF
>testinittab.dir/etc/rc.d/rc3.d/S90http
>testinittab.dir/etc/rc.d/rc3.d/S30xxx~
>testinittab.dir/etc/rc.d/rc3.d/.S30xxx

ln testinittab.dir/etc/rc.d/rc3.d/S90http testinittab.dir/etc/rc.d/rc4.d/S90http
ln testinittab.dir/etc/rc.d/rc3.d/S90http testinittab.dir/etc/rc.d/rc0.d/K90http
ln testinittab.dir/etc/rc.d/rc3.d/S90http testinittab.dir/etc/rc.d/rc6.d/K90http

chmod 700 testinittab.dir/etc/rc.d/rc3.d/S90http

ln testinittab.dir/etc/rc.d/rc3.d/S90http testinittab.dir/etc/rc.d/rc3.d/S80http

if ./testinittab -i testinittab.dir/inittab \
	      -l testinittab.dir/local \
	      -o testinittab.dir/override \
	      -p testinittab.dir/share \
	      -R testinittab.dir/etc/rc.d \
	      -c testinittab.dir/vera >/dev/null
then
    echo "Did not detect inconsistent link"
fi

rm testinittab.dir/etc/rc.d/rc3.d/S80http

>testinittab.dir/etc/rc.d/rc1.d/S90http
chmod 700 testinittab.dir/etc/rc.d/rc1.d/S90http

if ./testinittab -i testinittab.dir/inittab \
	      -l testinittab.dir/local \
	      -o testinittab.dir/override \
	      -p testinittab.dir/share \
	      -R testinittab.dir/etc/rc.d \
	      -c testinittab.dir/vera >/dev/null
then
    echo "Did not detect inconsistent link"
fi

rm testinittab.dir/etc/rc.d/rc1.d/S90http

>testinittab.dir/etc/rc.d/network
chmod 700 testinittab.dir/etc/rc.d/network
ln -s ../network testinittab.dir/etc/rc.d/rc3.d/S80network.sh
ln -s ../network testinittab.dir/etc/rc.d/rc4.d/K80network.sh

$VALGRIND ./testinittab -i testinittab.dir/inittab \
	      -l testinittab.dir/local \
	      -o testinittab.dir/override \
	      -p testinittab.dir/share \
	      -R testinittab.dir/etc/rc.d \
	      -c testinittab.dir/vera >testinittab.out
diff -U 3 testinittab.out ${srcdir}/testinittab.txt
rm -rf testinittab.dir testinittab.tst testinittab.out
