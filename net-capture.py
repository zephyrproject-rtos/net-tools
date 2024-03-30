#!/usr/bin/env python3
#
# Copyright (c) 2021 Intel Corporation
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

import getopt
import socket
import sys
import os.path
from datetime import datetime
from scapy.all import *
from scapy.layers.can import CAN

interface = None
verbose = True
port = 4242
pcap = None
type = "Ether"
cooked = False
cooked_sllv1 = False
cooked_sllv2 = False

argv = sys.argv[1:]

def usage():
    print("Listen captured network data from Zephyr and save it " +
        "optionally to pcap file.\n")
    print(f"{sys.argv[0]} \\" +
          "\n\t-i | --interface <network interface>" +
          "\n\t\tListen this inferface for the data" +
          "\n\t[-p | --port <UDP port>]" +
          f"\n\t\tUDP port (default is {port}) where the capture data is received" +
          "\n\t[-q | --quiet]" +
          "\n\t\tDo not print packet information" +
          "\n\t[-t | --type <L2 type of the data>]" +
          f"\n\t\tScapy L2 type name of the UDP payload, default is {type}" +
          "\n\t[-w | --write <pcap file name>]" +
          "\n\t\tWrite the received data to file in PCAP format" +
          "\n\t[-c | --cooked-sllv1]" +
          "\n\t\tThe payload is Linux cooked mode v1 data" +
          "\n\t[-C | --cooked-sllv2]" +
          "\n\t\tThe payload is Linux cooked mode v2 data")

try:
    opts, args = getopt.getopt(argv,
                               'cChi:p:qt:w:',
                               ['cooked-sllv1', 'cooked-sllv2', 'help',
                                'interface=', 'port=',
                                'quiet', 'type=', 'write='])
except getopt.GetoptError as err:
    print(err)
    usage()
    sys.exit(2)

for o, a in opts:
    if o in ("-q", "--quiet"):
        verbose = False
    elif o in ("-h", "--help"):
        usage()
        sys.exit()
    elif o in ("-i", "--interface"):
        interface = a
    elif o in ("-p", "--port"):
        port = int(a)
    elif o in ("-t", "--type"):
        type = a
    elif o in ("-w", "--write"):
        pcap = a
    elif o in ("-c", "--cooked-sllv1"):
        cooked_sllv1 = True
        cooked = True
    elif o in ("-C", "--cooked-sllv2"):
        cooked_sllv2 = True
        cooked = True
    else:
        assert False, "unhandled option " + o

if interface == None:
    print("Network interface not set.\n")
    usage()
    sys.exit()

if pcap != None and os.path.exists(pcap):
    os.remove(pcap)

sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_BINDTODEVICE,
                interface.encode())
sock.bind(('', port))

def process_packet(data):
    packet = eval(type)(data)

    if cooked:
        if cooked_sllv1:
            packet = CookedLinux(_pkt = Packet.build(packet))
        else:
            packet = CookedLinuxV2(_pkt = Packet.build(packet))

        packet.show()

    if verbose:
        now = datetime.utcnow()
        hdr = now.strftime("%Y%m%dZ%H:%M:%S") + "." + str(now.microsecond)
        print('[%s] %s' % (hdr, packet.summary()))
    if pcap != None:
        wrpcap(pcap, packet, append=True)

while True:
    data, address = sock.recvfrom(4096)
    process_packet(data)
