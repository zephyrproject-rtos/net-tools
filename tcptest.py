#!/usr/bin/python3
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

import random
import select
import socket
import sys
import time

if not hasattr(socket, 'if_nametoindex'):
    print("Please Python3, this is incompatible with Python 2")
    sys.exit(1)

if len(sys.argv) != 3:
    print("Usage: %s interface echo-server-ipv6-address" % sys.argv[0])
    sys.exit(1)

def log(msg):
    print("%s: %s" % (time.asctime(), msg))

iface = sys.argv[1]
addr = sys.argv[2]
scope_id = socket.if_nametoindex(iface)

log("Connecting to [%s]:4242 (through interface %s, scope_id %d)" % (
    addr, iface, scope_id
))

sock = socket.socket(socket.AF_INET6, socket.SOCK_STREAM, socket.IPPROTO_TCP)
sock.connect((addr, 4242, 0, scope_id))

# Use non-blocking I/O so we can use select() to check for timeouts
sock.setblocking(False)

log("Connection established. Will begin sending data")

delta_avg = 0
delta_avg_len = 0
timeouts = 0
tx_errors = 0
rx_errors = 0
for current_n in range(0, 2**32, 10):
    # Wait up to 1 second between each packet transmitted
    time.sleep(random.random())

    log("Will try sending %d and read it back" % current_n)

    while True:
        _, wlist, _ = select.select([], [sock], [], 1)
        if wlist:
            break

        timeouts += 1
        log("Timeout while sending data to Zephyr, will try again")

    time_before_send = time.time()
    current_n_bytes = bytes(str(current_n), "ascii")
    sent_bytes = sock.send(current_n_bytes)
    if sent_bytes != len(current_n_bytes):
        tx_errors +=1
        log("Were not able to transmit %d (%d bytes), will try again" % (
            current_n, len(current_n_bytes)
        ))
        continue

    while True:
        rlist, _, _ = select.select([sock], [], [], 1)
        if rlist:
            break

        timeouts += 1
        log("Timeout while reading data from Zephyr, will try again")

    rcvd_bytes = sock.recv(len(current_n_bytes))
    if len(rcvd_bytes) != len(current_n_bytes):
        rx_errors += 1
        log("Got back %d bytes instead of %d. Will send again" % (
            len(rcvd_bytes), len(current_n_bytes)
        ))
        continue

    if rcvd_bytes != current_n_bytes:
        rx_errors += 1
        log("Got back '%s' but sent '%s'. Will try again" % (
            rcvd_bytes, current_n_bytes
        ))
        continue

    delta = time.time() - time_before_send
    log("Got %d back in %f seconds" % (current_n, delta))

    delta_avg += delta
    delta_avg_len += 1
    if delta_avg_len > 10:
        log("Roundtrip takes %f seconds on average" % (delta_avg / 10))

        delta_avg = 0
        delta_avg_len = 0

        if timeouts > 0 or rx_errors > 0 or tx_errors > 0:
            log("So far: %d timeouts, %d rx errors, %d tx errors" % (
                timeouts, rx_errors, tx_errors
            ))
        else:
            log("So far, so good: no timeouts or errors")
