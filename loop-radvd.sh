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

ctrl_c() {
    STOPPED=1
    kill `cat $PID_FILE`
}

mkdir -p $PID_DIR

while [ $STOPPED -eq 0 ]; do
    radvd -n -C $DIR/radvd.conf -m stderr -p $PID_FILE -u root
done
