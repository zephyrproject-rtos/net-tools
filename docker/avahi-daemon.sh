#!/bin/sh

ip link set eth0 multicast on

avahi-daemon -f /etc/avahi-daemon.conf
