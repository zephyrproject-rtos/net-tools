#!/bin/bash
#
# Copyright (c) 2020 Intel Corporation
# SPDX-License-Identifier: Apache-2.0

# Simple TCP tester that queries a large HTTP file and checks whether it
# contains the expected data. The network sample to respond these queries is
# samples/net/sockets/dumb_http_srv_mt. With this sample we are not testing
# HTTP because this sample does not implement HTTP server but always returns
# a static file.

if [ `basename $0` == "https-get-file-test.sh" ]; then
    PROTO=https
    EXTRA_OPTS="--insecure"
else
    PROTO=http
    EXTRA_OPTS=""
fi

SERVER=192.0.2.1
PORT=8080

# The file we are expecting to receive is found in
# samples/net/sockets/dumb_http_server_mt/src/response_100k.html.bin
EXPECTED_FILE_SIZE=108222
EXPECTED_FILE_MD5SUM=d969441601d473db911f28dae62101b7

# If user gives a parameter to this function, it is used as a count how
# many times to ask the data. The value 0 (default) loops forever.

if [ -z "$1" ]; then
    COUNT=0
else
    COUNT=$1
fi

DOWNLOAD_FILE=/tmp/`basename $0`.$$
MD5SUM_FILE=${DOWNLOAD_FILE}.md5sum

echo "$EXPECTED_FILE_MD5SUM $DOWNLOAD_FILE" > $MD5SUM_FILE

CONNECTION_TIMEOUT=0
STATUS=0
STOPPED=0
trap ctrl_c INT TERM

ctrl_c() {
    STOPPED=1
}

while [ $STOPPED -eq 0 ]; do
    curl ${PROTO}://${SERVER}:${PORT} $EXTRA_OPTS --max-time 5 --output $DOWNLOAD_FILE
    STATUS=$?
    if [ $STATUS -ne 0 ]; then
	if [ $STATUS -eq 28 ]; then
	    CONNECTION_TIMEOUT=1
	fi

	break
    fi

    FILE_SIZE=$(stat --printf=%s $DOWNLOAD_FILE)
    if [ $FILE_SIZE -ne $EXPECTED_FILE_SIZE ]; then
        echo "Wrong size, expected $EXPECTED_FILE_SIZE, got $FILE_SIZE"
        STATUS=1
	break
    fi

    md5sum --status -c $MD5SUM_FILE
    STATUS=$?
    if [ $STATUS -ne 0 ]; then
	break
    fi

    if [ $COUNT -eq 0 ]; then
	continue
    fi

    COUNT=$(expr $COUNT - 1)
    if [ $COUNT -eq 0 ]; then
	break
    fi
done

rm -f $DOWNLOAD_FILE $MD5SUM_FILE

if [ $CONNECTION_TIMEOUT -eq 0 ]; then
    if [ $STATUS -eq 0 ]; then
	curl -d "OK" -X POST $EXTRA_OPTS ${PROTO}://${SERVER}:${PORT}
    else
	curl -d "FAIL" -X POST $EXTRA_OPTS ${PROTO}://${SERVER}:${PORT}
    fi
fi

exit $STATUS
