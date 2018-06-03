#/bin/sh
#set -x

# set the following flag to 1 to see output
OUTFLAG=0

PWD=`pwd`
SRC=/home/cs3231/assigns/asst3/test_adv
RUN="/home/cs3231/bin/realtime 30 20 /home/cs3231/bin/sys161 -X"

# check we are in the correct place in the file system (need to be run from the 'root' directory)
p=$PWD
while [[ $p != "root" &&  $p =~ "/" ]]; do
    p=`echo $p | cut -d \/ -f 2-`
done

if [[ $p != "root" ]]; then
    echo "Error: this scriopt needs to but run in the cs3231/root directory"
    exit 1
fi

# create location for the binaries
if [[ ! -d markbin ]]; then
    mkdir markbin
fi

# copy them from the class account

cp -ru $SRC/markbin .


echo "----------------------------------------"
echo -n "(0.6) Simple test of sbrk(0): "
cp $SRC/test-files/sys161-asst3.conf sys161.conf || exit 1
$RUN kernel "p /markbin/simple_sbrk" >out.log 2>&1
if  grep -q SUCCESS out.log ; then
    echo "success"
else
    echo "failed"
    if (($OUTFLAG)) ; then
	echo "----------------------------------------"
        cat out.log
    fi
fi

echo "----------------------------------------"
echo -n "(0.8) sbrk(0) with write to heap: "
cp $SRC/test-files/sys161-asst3.conf sys161.conf || exit 1
$RUN kernel "p /markbin/sbrk_write" >out.log 2>&1
if  grep -q SUCCESS out.log ; then
    echo "success"
else
    echo "failed"
    if (($OUTFLAG)) ; then
	echo "----------------------------------------"
        cat out.log
    fi
fi


echo "----------------------------------------"
echo -n "(0.6) User-level malloc stress test: "
cp $SRC/test-files/sys161-asst3.conf sys161.conf || exit 1
$RUN kernel "p /markbin/malloctest" >out.log 2>&1
if  grep -q SUCCESS out.log ; then
    echo "success"
else
    echo "failed"
    if (($OUTFLAG)) ; then
	echo "----------------------------------------"
        cat out.log
    fi
fi

echo "----------------------------------------"
echo -n "(3.0) COW 'huge' with check by 25 children: "
cp $SRC/test-files/sys161-asst3.conf sys161.conf || exit 1
$RUN kernel "p /markbin/cow_huge" >out.log 2>&1
if  grep -q SUCCESS out.log ; then
    echo "success"
else
    echo "failed"
    if (($OUTFLAG)) ; then
	echo "----------------------------------------"
        cat out.log
    fi
fi


echo "----------------------------------------"
echo -n "(1.5) Run 1 MiB executable in 1MiB of RAM: "
cp $SRC/test-files/sys161-lowmem.conf sys161.conf || exit 1
$RUN kernel "p /markbin/demand_load" >out.log 2>&1
if  grep -q SUCCESS out.log ; then
    echo "success"
else
    echo "failed"
    if (($OUTFLAG)) ; then
	echo "----------------------------------------"
        cat out.log
    fi
fi


echo "----------------------------------------"
echo -n "(0.5) Use mmap to read from a file: "
echo -n "The lazy dog tripped over the quick brown fox." > read.test
cp $SRC/test-files/sys161-asst3.conf sys161.conf || exit 1
$RUN kernel "p /markbin/mmap_read" >out.log 2>&1
if  grep -q SUCCESS out.log ; then
    echo "success"
else
    echo "failed"
    if (($OUTFLAG)) ; then
	echo "----------------------------------------"
        cat out.log
    fi
fi

echo "----------------------------------------"
echo -n "(0.5) Use mmap to read from two files: "
echo -n "The lazy dog tripped over the quick brown fox." > read1.test
echo -n "Many interesting things happened to me today." > read2.test
cp $SRC/test-files/sys161-asst3.conf sys161.conf || exit 1
$RUN kernel "p /markbin/mmap_read2" >out.log 2>&1
if  grep -q SUCCESS out.log ; then
    echo "success"
else
    echo "failed"
    if (($OUTFLAG)) ; then
	echo "----------------------------------------"
        cat out.log
    fi
fi


echo "----------------------------------------"
echo -n "(0.5) Use mmap to write 1 page: "
cp $SRC/test-files/sys161-asst3.conf sys161.conf || exit 1
rm -f write.test
echo -n "Many interesting things happened to me today." > write.compare
$RUN kernel "p /markbin/mmap_write" >out.log 2>&1
if  { head -c 45 write.test 2>&1 | diff -q - write.compare >> out.log 2>&1 ; } ; then
    echo "success"
else
    echo "failed"
    if (($OUTFLAG)) ; then
	echo "----------------------------------------"
        cat out.log
    fi
fi

echo "----------------------------------------"
echo -n "(1.0) Use mmap to write 4 pages: "
rm -f mmap_write4pages.test
cp $SRC/test-files/sys161-asst3.conf sys161.conf || exit 1
$RUN kernel "p /markbin/mmap_write4pages" >out.log 2>&1
if  diff -q markbin/mmap_write4pages.sample mmap_write4pages.test >> out.log 2>&1 ; then
    echo "success"
else
    echo "failed"
    if (($OUTFLAG)) ; then
	echo "----------------------------------------"
        cat out.log
    fi
fi

echo "----------------------------------------"
echo -n "(1.0) mmap version of 'huge' with no munmap: "
cp $SRC/test-files/sys161-asst3.conf sys161.conf || exit 1
$RUN kernel "p /markbin/mmap_huge" >out.log 2>&1
if  grep -q SUCCESS out.log ; then
    echo "success"
else
    echo "failed"
    if (($OUTFLAG)) ; then
	echo "----------------------------------------"
        cat out.log
    fi
fi
echo "----------------------------------------"
