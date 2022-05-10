set -xe

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
config:testconfglobal/globalfile1 space:globalfile1 space
config:testconfglobal/globalfile1:globalfile1
config:testconfglobal/sub2/inconfoverride:sub2/inconfoverride
config:testconflocal/sub1/inconflocal:sub1/inconflocal
config:testconfoverride/inconfoverride2:inconfoverride2
error:testconfglobal/.file1.invalid:ignoring non-compliant filename
error:testconfglobal/file1,invalid:ignoring non-compliant filename
error:testconfglobal/file1..invalid:ignoring non-compliant filename
error:testconfglobal/file1.invalid.:ignoring non-compliant filename
EOF

$VALGRIND ./testprocloader dump testconfglobal testconflocal testconfoverride \
	  >testprocloader.out

LC_ALL=C sort <testprocloader.out | diff -U 3 testprocloader.txt -

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
