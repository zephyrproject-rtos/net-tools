#!/usr/bin/env python3
#
# Copyright (c) 2020 Intel Corporation
# SPDX-License-Identifier: Apache-2.0
#
# Listen syslog UDP packets and verify that we received all of them for
# testing purposes.

import socket
import sys
import re

localPort   = 514
bufferSize  = 1024

def main(localIP):
    if ":" in localIP:
        family = socket.AF_INET6
    else:
        family = socket.AF_INET

    UDPServerSocket = socket.socket(family=family,
                                    type=socket.SOCK_DGRAM)

    UDPServerSocket.bind((localIP, localPort))

    if ":" in localIP:
        local_str = "[" + localIP + "]:" + str(localPort)
    else:
        local_str = localIP + ":" + str(localPort)

    print("syslog UDP server up and listening", local_str)

    recv_count_err = 0
    recv_count_wrn = 0
    recv_count_inf = 0
    recv_count_dbg = 0
    stopped = -1

    while(True):
        bytesAddressPair = UDPServerSocket.recvfrom(bufferSize)

        message = bytesAddressPair[0]
        address = bytesAddressPair[1]

        # Format of the message
        #<135>1 1970-01-01T00:01:03.330000Z :: - - - - net_syslog.main: Debug message
        items = re.split("\<([0-9]*)\>1 ([TZ\:\.\-0-9]*) ([0-9a-fA-F\:\.]*) ([\w-]) ([\w-]) ([\w-]) ([\w-]) net_syslog\S* (.*)'", format(message))

        if (len(items) > 1):
            #print("Items:{}".format(items))

            msg_type = re.split("(\S*) message \(\d\)", items[8])
            if len(msg_type) > 1:
                if msg_type[1] == "Error":
                    recv_count_err += 1
                elif msg_type[1] == "Warning":
                    recv_count_wrn += 1
                elif msg_type[1] == "Info":
                    recv_count_inf += 1
                elif msg_type[1] == "Debug":
                    recv_count_dbg += 1

                continue

            stopped_values = re.split("Stopped after (\d) msg", items[8])
            if len(stopped_values) > 1:
                stopped = int(stopped_values[1])
                break

    if recv_count_err == stopped and recv_count_wrn == stopped and \
       recv_count_inf == stopped and recv_count_dbg == stopped:
        print("OK")
        return 0

    print("FAIL")
    return -1

if __name__ == '__main__':
    if len(sys.argv) > 1:
        ret = main(sys.argv[1])
    else:
        ret = main('::')

    exit(ret)
