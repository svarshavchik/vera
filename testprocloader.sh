set -xe
export LC_ALL=C

rm -rf testconfglobal
rm -rf testconflocal
rm -rf testconfoverride

mkdir -p testconfglobal/sub1 testconfglobal/sub2 \
      testconflocal/sub1 testconfoverride/sub2

echo "" >testconfglobal/file1..invalid
echo "" >testconfglobal/.file1.invalid
echo "" >testconfglobal/file1.invalid.
echo "" >testconfglobal/file1,invalid

echo "" >testconfglobal/globalfile1
echo "" >"testconfglobal/globalfile1 space"

echo "" >testconfglobal/sub1/inconflocal
echo "" >testconflocal/sub1/inconflocal

echo "" >testconfglobal/sub2/inconfoverride
echo "" >testconfoverride/sub2/inconvoverride

echo "" >testconfglobal/inconfoverride2
echo "" >testconflocal/inconfoverride2
echo "" >testconfoverride/inconfoverride2

echo "" >testconflocal/nothere1
echo "" >testconfoverride/notthere2

cat >testprocloader.txt <<EOF
config:*:*:testconfglobal/globalfile1 space:globalfile1 space
config:*:*:testconfglobal/globalfile1:globalfile1
config:*:*:testconfglobal/sub2/inconfoverride:sub2/inconfoverride
config:*:testconflocal/sub1/inconflocal:testconfglobal/sub1/inconflocal:sub1/inconflocal
config:testconfoverride/inconfoverride2:testconflocal/inconfoverride2:testconfglobal/inconfoverride2:inconfoverride2
error:testconfglobal/.file1.invalid:ignoring non-compliant filename
error:testconfglobal/file1,invalid:ignoring non-compliant filename
error:testconfglobal/file1..invalid:ignoring non-compliant filename
error:testconfglobal/file1.invalid.:ignoring non-compliant filename
EOF

$VALGRIND ./testprocloader dump testconfglobal testconflocal testconfoverride \
	  >testprocloader.out

sort <testprocloader.out | diff -U 3 testprocloader.txt -

rm -rf testprocloader.txt testprocloader.out
rm -rf testconfglobal
rm -rf testconflocal
rm -rf testconfoverride

# -----

mkdir -p testconfglobal/sub1
mkdir -p testconfglobal/sub4
mkdir -p testconflocal/sub1
mkdir -p testconflocal/sub2
mkdir -p testconfoverride/sub1
mkdir -p testconfoverride/sub3

echo "" >testconfglobal/.invalid
echo "" >testconflocal/.invalid
echo "" >testconfoverride/.invalid

echo "" >testconfglobal/sub1/keep1
echo "" >testconfglobal/sub1/keep2
echo "" >testconflocal/sub1/keep1
echo "" >testconfoverride/sub1/keep2

echo "" >testconflocal/goaway1
echo "" >testconfoverride/goaway1

echo "" >testconflocal/goaway2
echo "" >testconfoverride/sub3/goaway3

echo "" >testconfglobal/otherdir1
mkdir testconflocal/otherdir1
echo "" >testconflocal/otherdir1/otherdir1goaway

mkdir testconfglobal/otherdir2
echo "" >testconfglobal/otherdir2/otherdir2keep
echo "" >testconfoverride/otherdir2

$VALGRIND ./testprocloader gc testconfglobal testconflocal testconfoverride

test ! -e testconfglobal/sub4
test ! -e testconfglobal/.invalid
test ! -e testconflocal/.invalid
test ! -e testconfoverride/.invalid

test -e testconfglobal/sub1/keep1
test -e testconfglobal/sub1/keep2
test -e testconflocal/sub1/keep1
test -e testconfoverride/sub1/keep2

test ! -e testconflocal/goaway1
test ! -e testconfoverride/goaway1

test ! -e testconflocal/goaway2
test ! -e testconfoverride/sub3/goaway3

test ! -e testconflocal/sub2
test ! -e testconfoverride/sub3

test ! -e testconflocal/otherdir1

test -e testconfglobal/otherdir2
test ! -e testconfoverride/otherdir2

rm -rf testconfglobal testconflocal testconfoverride

cat >loadtest.txt <<EOF
name: built-in
requires: [ 'built-in/subunit', 'some/other/unit' ]
Required-By: runlevel1
---
name: subunit
requires: /some/other/unit/again
Required-By:
    - prereq1
    - prereq2
version: 1
EOF

$VALGRIND ./testprocloader loadtest <loadtest.txt >loadtest.out

cat >loadtest.expected <<EOF
built-in:start=forking:stop=manual
built-in:requires built-in/subunit
built-in:requires some/other/unit
built-in:required-by runlevel1

built-in/subunit:start=forking:stop=manual
built-in/subunit:requires some/other/unit/again
built-in/subunit:required-by built-in/prereq1
built-in/subunit:required-by built-in/prereq2
EOF

cat loadtest.expected
diff -U 3 loadtest.expected loadtest.out

>loadtest.txt
$VALGRIND ./testprocloader loadtest <loadtest.txt >loadtest.out
cat loadtest.out
test ! -s loadtest.out

echo 'name: built-in' >loadtest.txt
$VALGRIND ./testprocloader loadtest <loadtest.txt >loadtest.out
cat >loadtest.txt <<EOF
error: (built-in): did not see a "version: 1" tag
EOF
diff -U 3 loadtest.txt loadtest.out

echo 'name: other-unit' >loadtest.txt
$VALGRIND ./testprocloader loadtest <loadtest.txt >loadtest.out
cat >loadtest.txt <<EOF
error: "other-unit": does not match its filename
EOF
diff -U 3 loadtest.txt loadtest.out

cat >loadtest.txt <<EOF
name: built-in
---
name: sub/unit
starting:
   type: oneshot
stopping:
   type: automatic
version:
  - 1
EOF

$VALGRIND ./testprocloader loadtest <loadtest.txt >loadtest.out
cat >loadtest.txt <<EOF
built-in:start=forking:stop=manual

built-in/sub/unit:start=oneshot:stop=automatic
EOF
diff -U 3 loadtest.txt loadtest.out

cat >loadtest.txt <<EOF
name: built-in
---
name: /sub/unit
version:
  - 2
  - 1
EOF

$VALGRIND ./testprocloader loadtest <loadtest.txt >loadtest.out
cat >loadtest.txt <<EOF
error: "/sub/unit": non-compliant name
EOF
diff -U 3 loadtest.txt loadtest.out

cat >loadtest.txt <<EOF
name: built-in
---
name: sub/unit/
version: 1
EOF
$VALGRIND ./testprocloader loadtest <loadtest.txt >loadtest.out
cat >loadtest.txt <<EOF
error: "sub/unit/": non-compliant name
EOF
diff -U 3 loadtest.txt loadtest.out

cat >loadtest.txt <<EOF
name: built-in
---
name: sub&unit
version: 1
EOF
$VALGRIND ./testprocloader loadtest <loadtest.txt >loadtest.out
cat >loadtest.txt <<EOF
error: "sub&unit": non-compliant name
EOF
diff -U 3 loadtest.txt loadtest.out

cat >loadtest.txt <<EOF
name: built-in
---
name: sub.un-it
version:
  - 1
EOF

$VALGRIND ./testprocloader loadtest <loadtest.txt >loadtest.out
cat >loadtest.txt <<EOF
built-in:start=forking:stop=manual

built-in/sub.un-it:start=forking:stop=manual
EOF
diff -U 3 loadtest.txt loadtest.out

cat >loadtest.txt <<EOF
name: built-in
---
name: sub..unit
version: 1
EOF
$VALGRIND ./testprocloader loadtest <loadtest.txt >loadtest.out
cat >loadtest.txt <<EOF
error: "sub..unit": non-compliant name
EOF
diff -U 3 loadtest.txt loadtest.out

cat >loadtest.txt <<EOF
- foo
EOF
$VALGRIND ./testprocloader loadtest <loadtest.txt >loadtest.out
cat >loadtest.txt <<EOF
error: built-in: bad format, expected a key/value map
EOF
diff -U 3 loadtest.txt loadtest.out

cat >loadtest.txt <<EOF
name: built-in
requires:
   foo: bar
version: 1
EOF
$VALGRIND ./testprocloader loadtest <loadtest.txt >loadtest.out
cat >loadtest.txt <<EOF
error: built-in: requires: bad format, expected a sequence (list)
EOF
diff -U 3 loadtest.txt loadtest.out

cat >loadtest.txt <<EOF
name: built-in
requires: built-in/sub1
---
name: sub1
requires: .
---
name: sub2
requires: sub1

version: 1
EOF
$VALGRIND ./testprocloader loadtest <loadtest.txt >loadtest.out

cat >loadtest.txt <<EOF
built-in:start=forking:stop=manual
built-in:requires built-in/sub1

built-in/sub1:start=forking:stop=manual
built-in/sub1:requires built-in

built-in/sub2:start=forking:stop=manual
built-in/sub2:requires built-in/sub1
EOF
diff -U 3 loadtest.txt loadtest.out

cat >loadtest.txt <<EOF
name: built-in
requires: built-in/sub1
---
name: sub1
requires: .
---
name: sub1
requires: sub1

version: 1
EOF
$VALGRIND ./testprocloader loadtest <loadtest.txt >loadtest.out

cat >loadtest.txt <<EOF
error: built-in/sub1: each unit must have a unique name
EOF
diff -U 3 loadtest.txt loadtest.out

cat >loadtest.txt <<EOF
name: built-in
starting:
    command: /bin/true
    timeout: 120
    before:
       - dep1
       - dep2
    after:
       - dep3
       - dep4
stopping:
    command: /bin/false
    timeout: 180
    before:
       - dep5
       - dep6
    after:
       - dep7
       - dep8
version: 1
EOF
$VALGRIND ./testprocloader loadtest <loadtest.txt >loadtest.out
cat >loadtest.txt <<EOF
built-in:start=forking:stop=manual
built-in:starting:/bin/true
built-in:stopping:/bin/false
built-in:starting_timeout 120
built-in:stopping_timeout 180
built-in:starting_before dep1
built-in:starting_before dep2
built-in:starting_after dep3
built-in:starting_after dep4
built-in:starting_before dep5
built-in:starting_before dep6
built-in:stopping_after dep7
built-in:stopping_after dep8
EOF
diff -U 3 loadtest.txt loadtest.out

cat >loadtest.txt <<EOF
name: built-in
required-by: one
enabled: graphical
version: 1
EOF

$VALGRIND ./testprocloader loadtest <loadtest.txt >loadtest.out

cat >loadtest.expected <<EOF
built-in:start=forking:stop=manual
built-in:required-by graphical
built-in:required-by one
EOF
diff -U 3 loadtest.expected loadtest.out

$VALGRIND ./testprocloader disabledloadtest <loadtest.txt >loadtest.out

cat >loadtest.expected <<EOF
built-in:start=forking:stop=manual
built-in:required-by one
EOF
diff -U 3 loadtest.expected loadtest.out

mkdir -p overridedir

$VALGRIND ./testprocloader setoverride overridedir .bad mask >loadtest.out
cat >loadtest.txt <<EOF
.bad: non-compliant filename
EOF
diff -U 3 loadtest.txt loadtest.out

$VALGRIND ./testprocloader setoverride overridedir sub/dir masked

test -f overridedir/sub/dir

$VALGRIND ./testprocloader setoverride overridedir sub/dir none

test ! -f overridedir/sub/dir

test ! -d overridedir/sub

test -d overridedir

$VALGRIND ./testprocloader setoverride overridedir sub/dir enabled

cat >loadtest.txt <<EOF
enabled
EOF
diff -U 3 loadtest.txt overridedir/sub/dir

rm -rf overridedir globaldir localdir

mkdir -p overridedir globaldir localdir overridedir

cat >globaldir/globalunit1 <<EOF
name: unit1
version: 1
EOF

cat >globaldir/unit2-runlevel1 <<EOF
name: unit2-runlevel1
enabled: runlevel1
version: 1
EOF

cat >globaldir/unit3-runlevel2 <<EOF
name: unit3-runlevel2
enabled: runlevel1
version: 1
EOF

cat >globaldir/unit4-disabled <<EOF
name: unit4-disabled
enabled: runlevel1
version: 1
EOF

cat >globaldir/unit5-masked <<EOF
name: unit5-masked
enabled: runlevel1
version: 1
EOF

cat >localdir/unit3-runlevel2 <<EOF
name: unit3-runlevel2
enabled: runlevel2
version: 1
EOF

>globaldir/.temporary

$VALGRIND ./testprocloader setoverride overridedir unit2-runlevel1 enabled >loadtest.out
$VALGRIND ./testprocloader setoverride overridedir unit3-runlevel2 enabled >loadtest.out
$VALGRIND ./testprocloader setoverride overridedir unit5-masked masked >loadtest.out

$VALGRIND ./testprocloader loadalltest globaldir localdir overridedir >loadtest.out

cat loadtest.out
cat >loadtest.txt <<EOF
W: globaldir/.temporary: ignoring non-compliant filename
E: "unit1": does not match its filename
unit2-runlevel1:start=forking:stop=manual
unit2-runlevel1:required-by runlevel1

unit3-runlevel2:start=forking:stop=manual
unit3-runlevel2:required-by runlevel2

unit4-disabled:start=forking:stop=manual
EOF

diff -U 3 loadtest.txt loadtest.out

./testprocloader testrunlevelconfig loadtest.txt

cat >loadtest.txt <<EOF
# Comments should be ignored
name: built-in
description: >-
    Long
    multiline description
version: 1
EOF
$VALGRIND ./testprocloader loadtest <loadtest.txt >loadtest.out
cat >loadtest.expected <<EOF
built-in:start=forking:stop=manual
built-in:description=Long multiline description
EOF

diff -U 3 loadtest.expected loadtest.out

rm -rf globaldir
mkdir -p globaldir
./testprocloader genrunlevels loadtest.txt globaldir
cd globaldir
for f in *
do
    ../testprocloader validatetest $f "system/$f" . ../localdir ../configdir
done
cd ..
rm -rf globaldir localdir overridedir
mkdir -p globaldir localdir overridedir

cat >globaldir/name1 <<EOF
name: name1
version: 1
EOF

cat >globaldir/name2 <<EOF
name: name1
version: 1
EOF

cat >globaldir/name3 <<EOF
name: name1
version: 1
EOF

echo "masked" >overridedir/name1

echo "enabled" >overridedir/name2

cat >loadtest.txt <<EOF
name1:masked:0
name2/a:stopped:1
name2/b:stopped:1
name2:stopped:1
name3:stopped:0
EOF

$VALGRIND ./testprocloader testupdatestatusoverrides name2 name2/a name2/b name3 | sort >loadtest.out

diff -U 3 loadtest.txt loadtest.out

rm -rf globaldir localdir overridedir \
    loadtest.txt loadtest.out loadtest.expected
