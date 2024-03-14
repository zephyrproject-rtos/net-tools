#!/bin/bash
#
# Copyright (c) 2024 Nordic Semiconductor
# SPDX-License-Identifier: Apache-2.0
#
# Start dnsmasq and http server and listen connections. This script is run
# inside Docker container.

dnsmasq -C /etc/dnsmasq.conf &
/usr/local/bin/http-server.py &
/usr/local/bin/https-server.py
