#!/usr/bin/env sh
#
# Set up the mac80211_hwsim Wi-Fi test network, see README.wifi.
# The configuration files are looked up relative to the net-tools directory.

cd "${0%/*}" || exit 1
exec ./net-setup.sh -c zwifi.conf -i zwifi "$@"
