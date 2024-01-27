set -xe
export LC_ALL=C
umask 022
srcdir="${srcdir-.}"

rm -rf testinittab.dir
mkdir -p testinittab.dir/vera/system
mkdir -p testinittab.dir/local
mkdir -p testinittab.dir/override
mkdir -p testinittab.dir/etc
$VALGRIND ./testprocloader genrunlevels testinittab.dir/runlevels testinittab.dir/vera/system

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
$VALGRIND ./testinittab -i testinittab.dir/inittab \
	      -l testinittab.dir/local \
	      -o testinittab.dir/override \
	      -c testinittab.dir/vera >testinittab.out
diff -U 3 testinittab.out ${srcdir}/testinittab.txt
rm -rf testinittab.dir testinittab.tst testinittab.out
