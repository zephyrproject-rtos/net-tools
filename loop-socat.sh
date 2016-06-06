#!/bin/sh
# Run socat in a loop. This way we can restart qemu and do not need
# to manually restart socat.

STOPPED=0
trap ctrl_c INT TERM

function ctrl_c() {
    STOPPED=1
}

while [ $STOPPED -eq 0 ]; do
    socat PTY,link=/tmp/slip.dev UNIX-LISTEN:/tmp/slip.sock
done
