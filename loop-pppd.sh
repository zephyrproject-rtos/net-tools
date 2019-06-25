#!/bin/bash
#
# Copyright (c) 2019 Intel Corporation
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

DEV=/tmp/ppp.dev

if [ ! -L "$DEV" ]; then
    echo "The $DEV is not a symbolic link to PTY."
    echo "Please run loop-ppp-dev.sh script to create it."
    exit 1
fi

PTS_DEV=`find $DEV -ls -type l | awk -F' ' '{ print $13 }'`

STOPPED=0
trap ctrl_c INT TERM

ctrl_c() {
    STOPPED=1
}

while [ $STOPPED -eq 0  ]; do
    pppd $PTS_DEV +ipv6 local \
	 noauth novj noccp noaccomp nodefaultroute nomp nodetach \
	 debug silent 192.0.2.2:192.0.2.1
    sleep 2
done
