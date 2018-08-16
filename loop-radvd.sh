#!/bin/sh
#
# Copyright (c) 2016 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Run radvd in a loop. This way we can restart qemu and do not need
# to manually restart radvd process.

PID_DIR=/var/run/radvd
PID_FILE=$PID_DIR/radvd.pid

ip link | grep tap0 > /dev/null 2>&1
if [ $? -eq 0 ]; then
    CONF_FILE=radvd_slip.conf
else
    ip link | grep zeth > /dev/null 2>&1
    if [ $? -eq 0 ]; then
	CONF_FILE=radvd_native_posix.conf
    else
	echo "Cannot find suitable network interface to run radvd"
	exit 3
    fi
fi

if [ ! -f $CONF_FILE ]; then
    if [ ! -f $ZEPHYR_BASE/../net-tools/$CONF_FILE ]; then
	echo "Cannot find $CONF_FILE file."
	exit 1
    fi
    DIR=$ZEPHYR_BASE/../net-tools
else
    DIR=.
fi

if [ ! -f $DIR/$CONF_FILE ] ;then
    echo "No such config file $DIR/$CONF_FILE found."
    exit 4
fi

if [ ! -s $DIR/$CONF_FILE ]; then
    echo "Config file $DIR/$CONF_FILE is empty."
    exit 4
fi

if [ `id -u` != 0 ]; then
    echo "Run this script as a root user!"
    exit 2
fi

STOPPED=0
trap ctrl_c INT TERM

ctrl_c() {
    STOPPED=1
    kill `cat $PID_FILE`
}

mkdir -p $PID_DIR

while [ $STOPPED -eq 0 ]; do
    radvd -n -C $DIR/$CONF_FILE -m stderr -p $PID_FILE -u root
done
