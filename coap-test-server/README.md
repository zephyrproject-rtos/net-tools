***

# COAP Test Server Script Helpers

## Overview

This directory contains helper scripts for manual testing of the  
`coap_client` sample.

The main helper script is:

    coap_sample_server.py

It runs a small CoAP server intended for use on a Linux host such as a
Raspberry Pi. The server provides the resource paths expected by the
Zephyr sample client:

    /test
    /large
    /obs

These resources are useful when validating:

*   simple GET, PUT, POST, and DELETE exchanges
*   block-wise GET transfer on `/large`
*   observe registration and notifications on `/obs`

## Requirements

Install the Python dependency listed in:

    requirements-coap-server.txt

Example:

```console
python3 -m pip install -r requirements-coap-server.txt
```

## Usage

Run the helper server on the host that will receive CoAP requests from the
Zephyr device:

```console
python3 coap_sample_server.py
```

By default the server listens on UDP port `5683` on all interfaces.

The script logs basic request activity and exposes responses that match the
behavior expected by the sample client.

## Network Notes

If the server is running on a remote host or behind a router, ensure that:

*   UDP port `5683` is reachable from the Zephyr device
*   any host firewall allows inbound UDP traffic on port `5683`
*   DNS resolves to the correct server if the client uses a hostname

## Limitations

This helper is intended for development and interoperability testing.  
It is **not** intended to be a production CoAP service implementation.

***
