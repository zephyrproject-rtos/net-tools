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

# Run socat in a loop. This way we can restart qemu and do not need
# to manually restart socat.

STOPPED=0
trap ctrl_c INT TERM

ctrl_c() {
    STOPPED=1
}

# If this file entry already exists, socat may complain
rm -f /tmp/slip.sock

while [ $STOPPED -eq 0 ]; do
    socat PTY,link=/tmp/slip.dev UNIX-LISTEN:/tmp/slip.sock
done
