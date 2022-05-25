[![Run Status](https://api.shippable.com/projects/58ffb2b81fb3ec0700e1602f/badge?branch=master)](https://app.shippable.com/github/zephyrproject-rtos/net-tools)

# Networking Tools

The comments and instructions below are for the new IP stack in Zephyr.

Here are instructions how to communicate between Zephyr that is running
inside QEMU, and host device that is running Linux.

For setting up routing and NAT rules to allow access to external networks, please see
the [README NAT.md](README%20NAT.md)

You need to run *socat* and *tunslip* to create a minimally working
network setup.

There are convenience scripts (_loop-socat.sh_ and _loop-slip-tap.sh_) for
running socat and tunslip6 processes. For running these, you need two
terminals.

Terminal 1:
```
$ ./loop-socat.sh
```

Terminal 2:
```
$ sudo ./loop-slip-tap.sh
```

After running these scripts you do not need to manually restart
them when qemu process stops.

In the Qemu side, you need to compile the kernel with proper config.
Minimally you need these settings active in your project config file.
```
CONFIG_NETWORKING=y
CONFIG_NET_IPV6=y
CONFIG_NET_IPV4=y
CONFIG_NET_YAIP=y
CONFIG_NET_UDP=y
CONFIG_NET_LOG=y
CONFIG_NET_SLIP=y
CONFIG_SLIP_TAP=y
CONFIG_SYS_LOG=y
CONFIG_SYS_LOG_SHOW_COLOR=y
CONFIG_NANO_TIMEOUTS=y
CONFIG_TEST_RANDOM_GENERATOR=y
```

After you have the loop scripts and Qemu running running you can communicate
with the Zephyr.

If your have echo-server running in the Qemu, then you can use the echo-client
tool in net-tools directory to communicate with it.
```
# ./echo-client -i tap0 2001:db8::1
```
The IP stack responds to ping requests if properly configured.
```
$ ping6 -I tap0 -c 1 2001:db8::1
```
You can attach wireshark to tap0 interface to see what data is being
transferred.

If building with CONFIG_NET_TCP=y in your project config file, it's possible
to run the echo-server sample in Zephyr, and then test the TCP stack using
the supplied tcptest.py script:
```
$ ./tcptest.py tap0 2001:db8::1
```
This script will send numbers to the echo-server program, read them back,
and compare if it got the exact bytes back.  Transmission errors, timeouts,
and time to get the response are all recorded and printed to the standard
output.

Be sure to use Python 3, as it requires a function from the socket module
that's only available in this version (wrapper around if_nametoindex(3)).


## Using net-setup.sh script to setup host side ethernet interface

The net-setup.sh script can setup an ethernet interface to the host.
User is able to setup a configuration file that will contain
commands to setup IP addresses and routes to the host interface.
This net-setup.sh script will need to be run as a root user.

If no parameters are given, then "zeth" network interface and "zeth.conf"
configuration file are used. The script waits until user presses CTRL-c
and then removes the network interface.
```
$ net-setup.sh
```
```
$ net-setup.sh --config zeth-vlan.conf
```
```
$ net-setup.sh --config my-own-config.conf --iface foobar
```

It is also possible to let the script return and then stop the network
interface later. Is can be done by first creating the interface with
"start" or "up" command, and then later remove the interface with
"stop" or "down" command.
```
$ net-setup.sh start
do your things here
$ net-setup.sh stop
```
```
$ net-setup.sh --config my-own-config.conf up
do your things here
$ net-setup.sh --config my-own-config.conf down
```

Any extra parameters that the script does not know, are passed directly
to "ip" command.
```
$ net-setup.sh --config my-own-config.conf --iface foo user bar
```

## Using encrypted SSL link with echo-* programs

Install stunnel

Fedora:
```
$ dnf install stunnel
```
Ubuntu:
```
$ apt-get install stunnel4 -y
```
Finally run the stunnel script in Linux
```
$ ./stunnel.sh
```
And connect echo-client to this SSL tunnel (note that the IP address
is the address of Linux host where the tunnel end point is located).
```
$ ./echo-client -p 4243 2001:db8::2 -t
```
If you are running echo-client in Zephyr QEMU, then run echo-server like
this:
```
$ ./echo-server -p 4244 -i tap0
```

If you want to re-create the certificates in echo-server and echo-client in
Zephyr net samples, then they can be created like this (note that you do not
need to do this as the certs have been prepared already in echo-server and
echo-client sample sources):
```
$ openssl genrsa -out echo-apps-key.pem 2048
$ openssl req -new -x509 -key echo-apps-key.pem -out echo-apps-cert.pem \
    -days 10000 -subj '/CN=localhost'
```
The cert that is to be embedded into test_certs.h in echo-server and
echo-client, can be generated like this:
```
$ openssl x509 -in echo-apps-cert.pem -outform DER | \
    hexdump -e '8/1 "0x%02x, " "\n"' | sed 's/0x  ,//g'
```
The private key to be embedded into test_certs.h in echo-server can be
generated like this:
```
$ openssl pkcs8 -topk8 -inform PEM -outform DER -nocrypt \
    -in echo-apps-key.pem | hexdump -e '8/1 "0x%02x, " "\n"' | \
    sed 's/0x  ,//g'
```

If you want to re-create the signed certificates in echo-server in Zephyr
net samples and echo-client in net-tools, then they can be created like this
(note that you do not need to do this as the certs have been prepared already
in echo-server and echo-client sources):
```
CA
--
$ openssl genrsa -out ca_privkey.pem 2048
$ openssl req -new -x509 -days 36500 -key ca_privkey.pem -out ca.crt -subj "/CN=exampleCA"

Convert to DER format
$ openssl x509 -in ca.crt -outform DER -out ca.der
```

```
Client
------
$ openssl genrsa -out client_privkey.pem 2048
$ openssl req -new -key client_privkey.pem -out client.csr -subj "/CN=exampleClient"
$ openssl x509 -req -CA ca.crt -CAkey ca_privkey.pem -days 36500 -in client.csr -CAcreateserial -out client.crt
```

```
Server
------
$ openssl genrsa -out server_privkey.pem 2048
$ openssl req -new -key server_privkey.pem -out server.csr -subj "/CN=localhost"
$ openssl x509 -req -CA ca.crt -CAkey ca_privkey.pem -days 36500 -in server.csr -CAcreateserial -out server.crt

Convert to DER format
$ openssl x509 -in server.crt -outform DER -out server.der
$ openssl pkcs8 -topk8 -inform PEM -outform DER -nocrypt -in server_privkey.pem -out server_privkey.der
```

Copy ca.crt, client.crt and client_privkey.pem to net-tools.
Copy ca.der, server.der and server_privkey.der to samples/net/sockets/echo-server/src/.

Enable NET_SAMPLE_CERTS_WITH_SC in samples/net/sockets/echo-server and build the sample.
Use stunnel_sc.conf in stunnel.sh to run echo-client with signed certificates.

## Using DTLS link with echo-* programs

For DTLS client functionality, you can do this

```
$ ./dtls-client -c echo-apps-cert.pem 2001:db8::1
```
or
```
$ ./dtls-client -c echo-apps-cert.pem 192.0.2.1
```
For DTLS server functionality, you can do this

```
$ ./dtls-server
```

## TLS connecitivity errors

If you see this error print in zephyr console

[net/app] [ERR] _net_app_ssl_mainloop: Closing connection -0x7180 (SSL - Verification of the message MAC failed)

Then increasing the mbedtls heap size might help. So you can set the option
CONFIG_MBEDTLS_HEAP_SIZE to some higher value.

Example:
```
CONFIG_MBEDTLS_HEAP_SIZE=30000
```

## PPP Connectivity

You can test the PPP connectivity running in Qemu in Zephyr using pppd that is
running in Linux host. You need to run *socat* and *pppd* to create
a minimally working network setup.

There are convenience scripts (_loop-ppp-dev.sh_ and _loop-pppd.sh_) for
running socat and pppd processes. For running these, you need two
terminals.

Terminal 1:
```
$ ./loop-ppp-dev.sh
```

Terminal 2:
```
$ sudo ./loop-pppd.sh
```

After this, start PPP enabled Zephyr application. For example Zephyr
*echo-server* sample in samples/net/sockets/echo_server has _overlay-ppp.conf_
file that enables PPP support.
