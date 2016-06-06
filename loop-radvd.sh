#!/bin/sh
# Run radvd in a loop. This way we can restart qemu and do not need
# to manually restart radvd process.

PID_DIR=/var/run/radvd
PID_FILE=$PID_DIR/radvd.pid

if [ ! -f ./radvd.conf ]; then
    if [ ! -f $ZEPHYR_BASE/net/ip/tools/radvd.conf ]; then
	echo "Cannot find radvd.conf file."
	exit 1
    fi
    DIR=$ZEPHYR_BASE/net/ip/tools
else
    DIR=.
fi

if [ `id -u` != 0 ]; then
    echo "Run this script as a root user!"
    exit 2
fi

STOPPED=0
trap ctrl_c INT TERM

function ctrl_c() {
    STOPPED=1
    kill `cat $PID_FILE`
}

mkdir -p $PID_DIR

while [ $STOPPED -eq 0 ]; do
    radvd -n -d 1 -C $DIR/radvd.conf -m stderr -p $PID_FILE -u root
done
