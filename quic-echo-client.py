#!/usr/bin/env python3
#
# Copyright (c) 2026 Nordic Semiconductor
# SPDX-License-Identifier: Apache-2.0
#
"""
Simple aioquic-based QUIC client that performs a real QUIC + TLS handshake
to a given IP:port, sends data on a bidirectional stream, and verifies
that the server echoes it back correctly.

Usage:
  pip install aioquic cryptography
  # IPv4
  sudo python3 quic-echo-client.py --host 192.0.2.1 --port 4422
  # IPv6
  sudo python3 quic-echo-client.py --host 2001:db8::1 --port 4422

Notes:
- This performs a real QUIC handshake (TLS 1.3 underneath) using aioquic.
- By default this script disables certificate verification (so it can connect
  to an IP address without a matching certificate). For production, remove
  the line that disables verification and provide a proper server_name/SNI
  and CA bundle.
- The destination 192.0.2.1 is in TEST-NET-1 and isn't routable; ensure a
  QUIC server is listening at that IP:port before running.
- For IPv6, use --host with an IPv6 address.
"""
import argparse
import asyncio
import socket
import ssl
import logging
import os
import random

from aioquic.asyncio.client import connect
from aioquic.asyncio.protocol import QuicConnectionProtocol
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.events import StreamDataReceived, ConnectionTerminated


def setup_logging(level):
    """Configure logging based on debug level."""
    if level == 0:
        # Disable most logging
        logging.basicConfig(level=logging.WARNING)
        logging.getLogger("aioquic").setLevel(logging.WARNING)
        logging.getLogger("quic").setLevel(logging.WARNING)
    elif level == 1:
        # Info level
        logging.basicConfig(level=logging.INFO)
        logging.getLogger("aioquic").setLevel(logging.INFO)
        logging.getLogger("quic").setLevel(logging.INFO)
    else:
        # Debug level (level >= 2)
        logging.basicConfig(level=logging.DEBUG)
        logging.getLogger("aioquic").setLevel(logging.DEBUG)
        logging.getLogger("quic").setLevel(logging.DEBUG)


class EchoClientProtocol(QuicConnectionProtocol):
    """Custom protocol that tracks received data for echo verification."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.received_data = {}  # stream_id -> bytes
        self.stream_finished = {}  # stream_id -> bool
        self.data_received_event = asyncio.Event()

    def quic_event_received(self, event):
        if isinstance(event, StreamDataReceived):
            stream_id = event.stream_id
            if stream_id not in self.received_data:
                self.received_data[stream_id] = b""
            self.received_data[stream_id] += event.data
            if event.end_stream:
                self.stream_finished[stream_id] = True
            self.data_received_event.set()
        elif isinstance(event, ConnectionTerminated):
            self.data_received_event.set()


class TestStats:
    """Track test statistics."""

    def __init__(self):
        self.packets_sent = 0
        self.bytes_sent = 0
        self.packets_received = 0
        self.bytes_received = 0
        self.streams_used = 0
        self.streams_passed = 0
        self.streams_failed = 0
        self.mismatches = []

    def print_summary(self):
        print("\n" + "=" * 60)
        print("TEST SUMMARY")
        print("=" * 60)
        print(f"Streams used:     {self.streams_used}")
        print(f"Streams passed:   {self.streams_passed}")
        print(f"Streams failed:   {self.streams_failed}")
        print(f"Packets sent:     {self.packets_sent}")
        print(f"Bytes sent:       {self.bytes_sent}")
        print(f"Packets received: {self.packets_received}")
        print(f"Bytes received:   {self.bytes_received}")

        if self.mismatches:
            print("\nMISMATCHES:")
            for m in self.mismatches:
                print(f"  Stream {m['stream_id']}:")
                print(f"    Sent {m['sent_len']} bytes, received {m['recv_len']} bytes")
                if m['sent_len'] != m['recv_len']:
                    print(f"    Length mismatch!")
                else:
                    print(f"    Content mismatch at byte {m['first_diff']}")

        result = "PASS" if self.streams_failed == 0 else "FAIL"
        print(f"\nOverall result: {result}")
        print("=" * 60)


def format_address(host: str, port: int) -> str:
    """Format address for display, handling IPv6 brackets."""
    try:
        socket.inet_pton(socket.AF_INET6, host)
        return f"[{host}]:{port}"
    except socket.error:
        return f"{host}:{port}"


async def run_echo_test(host: str, port: int, num_packets: int,
                        packets_per_stream: int,
                        min_size: int, max_size: int, timeout: float,
                        insecure: bool = True):
    """Run echo test: send data and verify server echoes it back.
    
    Args:
        packets_per_stream: Number of packets to send on each stream.
                           0 means each packet uses a new stream.
                           -1 means all packets use the same stream.
    """

    stats = TestStats()

    config = QuicConfiguration(
        is_client=True,
        alpn_protocols=["echo-quic"],
        idle_timeout=60.0,  # Longer idle timeout for slow servers
    )
    if insecure:
        config.verify_mode = ssl.CERT_NONE

    print(f"Connecting to {format_address(host, port)} (insecure={insecure}) ...")

    try:
        connect_kwargs = {
            "configuration": config,
            "create_protocol": EchoClientProtocol,
        }

        async with connect(host, port, **connect_kwargs) as protocol:
            print("QUIC handshake completed.")

            quic = protocol._quic
            alpn = getattr(quic, "alpn_negotiated", None)
            print(f"Negotiated ALPN: {alpn}")

            current_stream_id = None
            packets_on_current_stream = 0
            stream_payload = b""  # Accumulate payload for multi-packet streams

            for i in range(num_packets):
                # Determine if we need a new stream
                need_new_stream = False
                if current_stream_id is None:
                    need_new_stream = True
                elif packets_per_stream == 0:
                    # Each packet on new stream
                    need_new_stream = True
                elif packets_per_stream > 0 and packets_on_current_stream >= packets_per_stream:
                    # Reached limit for this stream
                    need_new_stream = True
                # packets_per_stream == -1 means keep using same stream

                # If we need new stream and have pending data, verify previous stream first
                if need_new_stream and current_stream_id is not None and len(stream_payload) > 0:
                    # Close the previous stream and verify
                    quic.send_stream_data(current_stream_id, b"", end_stream=True)
                    protocol.transmit()

                    # Wait for response
                    success = await wait_and_verify_stream(
                        protocol, current_stream_id, stream_payload, timeout, stats)
                    if not success:
                        stats.print_summary()
                        return False

                    stream_payload = b""

                if need_new_stream:
                    # Get new stream ID
                    try:
                        current_stream_id = quic.get_next_available_stream_id(is_unidirectional=False)
                    except TypeError:
                        current_stream_id = quic.get_next_available_stream_id(False)
                    packets_on_current_stream = 0
                    stats.streams_used += 1
                    print(f"\n[Stream {current_stream_id}] New stream opened")

                # Generate random payload
                payload_size = random.randint(min_size, max_size)
                payload = os.urandom(payload_size)

                # Determine if this is the last packet on stream
                is_last_on_stream = False
                if packets_per_stream == 0:
                    # Each packet is alone on stream
                    is_last_on_stream = True
                elif packets_per_stream > 0:
                    is_last_on_stream = (packets_on_current_stream + 1 >= packets_per_stream)
                elif i == num_packets - 1:
                    # Last packet overall when using single stream
                    is_last_on_stream = True

                print(f"[Stream {current_stream_id}] Sending packet {i+1}/{num_packets}, {len(payload)} bytes" +
                      (" (end_stream)" if is_last_on_stream else ""))

                # Clear event
                protocol.data_received_event.clear()

                # Send data
                quic.send_stream_data(current_stream_id, payload, end_stream=is_last_on_stream)
                protocol.transmit()
                stats.packets_sent += 1
                stats.bytes_sent += len(payload)
                stream_payload += payload
                packets_on_current_stream += 1

                # If this ends the stream, wait for response and verify
                if is_last_on_stream:
                    success = await wait_and_verify_stream(
                        protocol, current_stream_id, stream_payload, timeout, stats)
                    if not success:
                        stats.print_summary()
                        return False
                    stream_payload = b""
                    current_stream_id = None

            # Handle any remaining data on open stream
            if current_stream_id is not None and len(stream_payload) > 0:
                quic.send_stream_data(current_stream_id, b"", end_stream=True)
                protocol.transmit()
                success = await wait_and_verify_stream(
                    protocol, current_stream_id, stream_payload, timeout, stats)
                if not success:
                    stats.print_summary()
                    return False

            print("\nAll packets completed. Closing connection.")

    except Exception as exc:
        print(f"Error during QUIC connection: {exc}")
        import traceback
        traceback.print_exc()
        stats.print_summary()
        return False

    stats.print_summary()
    return stats.streams_failed == 0


async def wait_and_verify_stream(protocol, stream_id, expected_payload, timeout, stats):
    """Wait for stream response and verify it matches expected payload."""
    wait_time = 0
    poll_interval = 0.1

    while wait_time < timeout:
        protocol.transmit()
        await asyncio.sleep(poll_interval)
        wait_time += poll_interval

        # Check if we got complete response
        if (stream_id in protocol.received_data and
            protocol.stream_finished.get(stream_id, False)):
            break

    received = protocol.received_data.get(stream_id, b"")
    stats.bytes_received += len(received)
    if received:
        stats.packets_received += 1

    # Verify response
    if received == expected_payload:
        print(f"[Stream {stream_id}] PASS - Echo matched ({len(received)} bytes)")
        stats.streams_passed += 1
        return True
    else:
        print(f"[Stream {stream_id}] FAIL - Echo mismatch!")
        print(f"  Sent:     {len(expected_payload)} bytes")
        print(f"  Received: {len(received)} bytes")
        stats.streams_failed += 1

        # Find first difference
        first_diff = -1
        for j in range(min(len(expected_payload), len(received))):
            if expected_payload[j] != received[j]:
                first_diff = j
                break
        if first_diff == -1 and len(expected_payload) != len(received):
            first_diff = min(len(expected_payload), len(received))

        stats.mismatches.append({
            'stream_id': stream_id,
            'sent_len': len(expected_payload),
            'recv_len': len(received),
            'first_diff': first_diff
        })

        # Show some context around mismatch
        if first_diff >= 0 and len(received) > 0:
            start = max(0, first_diff - 8)
            end = min(len(expected_payload), first_diff + 8)
            print(f"  Expected[{start}:{end}]: {expected_payload[start:end].hex()}")
            end = min(len(received), first_diff + 8)
            print(f"  Received[{start}:{end}]: {received[start:end].hex()}")

        return False


async def run_simple_send(host: str, port: int, payload: bytes,
                          insecure: bool = True):
    """Simple send without echo verification (original behavior)."""

    config = QuicConfiguration(
        is_client=True,
        alpn_protocols=["echo-quic"],
        idle_timeout=60.0,  # Longer idle timeout for slow servers
    )
    if insecure:
        config.verify_mode = ssl.CERT_NONE

    print(f"Connecting to {format_address(host, port)} (insecure={insecure}) ...")

    connect_kwargs = {"configuration": config}

    async with connect(host, port, **connect_kwargs) as protocol:
        print("QUIC handshake completed.")
        quic = protocol._quic

        alpn = getattr(quic, "alpn_negotiated", None)
        print(f"Negotiated ALPN: {alpn}")

        try:
            stream_id = quic.get_next_available_stream_id(is_unidirectional=False)
        except TypeError:
            stream_id = quic.get_next_available_stream_id(False)

        print(f"Opening stream {stream_id} and sending {len(payload)} bytes...")
        quic.send_stream_data(stream_id, payload, end_stream=True)

        # Force transmission
        protocol.transmit()

        await asyncio.sleep(1)
        print("Payload sent. Closing connection.")

    print("Connection context closed.")


def main():
    p = argparse.ArgumentParser(
        description="QUIC echo test client using aioquic.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Send 1 packet on 1 stream (default)
  %(prog)s --host 192.0.2.1 --port 4422

  # IPv6 connection
  %(prog)s --host 2001:db8::1 --port 4422

  # Send 10 packets all on the same stream
  %(prog)s --host 192.0.2.1 --packets 10

  # Send 10 packets, each on a separate stream
  %(prog)s --host 192.0.2.1 --packets 10 --packets-per-stream 0

  # Send 10 packets, 2 packets per stream (5 streams total)
  %(prog)s --host 192.0.2.1 --packets 10 --packets-per-stream 2

  # Echo test with specific payload size range
  %(prog)s --host 192.0.2.1 --min-size 100 --max-size 1024

  # Simple send without echo verification
  %(prog)s --host 192.0.2.1 --no-echo --message "hello"
""")
    p.add_argument("--host", default="192.0.2.1",
                   help="Destination IP (IPv4 or IPv6) or hostname")
    p.add_argument("--port", type=int, default=4422, help="Destination port")
    p.add_argument("--insecure", action="store_true", default=True,
                   help="Disable TLS certificate verification (default: True)")
    p.add_argument("--packets", type=int, default=1,
                   help="Number of packets to send (default: 1)")
    p.add_argument("--packets-per-stream", type=int, default=-1,
                   help="Packets per stream: -1=all on same stream (default), 0=each on new stream, N=N packets per stream")
    p.add_argument("--min-size", type=int, default=1,
                   help="Minimum payload size in bytes (default: 1)")
    p.add_argument("--max-size", type=int, default=256,
                   help="Maximum payload size in bytes (default: 256)")
    p.add_argument("--timeout", type=float, default=30.0,
                   help="Timeout in seconds waiting for echo response (default: 30.0)")
    p.add_argument("--message", default="hello from aioquic",
                   help="Message to send when using --no-echo")
    p.add_argument("--no-echo", action="store_true",
                   help="Simple send without waiting for echo response")
    p.add_argument("-d", "--debug", type=int, default=0, choices=[0, 1, 2],
                   help="Debug level: 0=off (default), 1=info, 2=debug")
    args = p.parse_args()

    setup_logging(args.debug)

    try:
        if args.no_echo:
            payload = args.message.encode("utf-8")
            asyncio.run(run_simple_send(args.host, args.port, payload,
                                        insecure=args.insecure))
        else:
            success = asyncio.run(run_echo_test(
                args.host, args.port,
                num_packets=args.packets,
                packets_per_stream=args.packets_per_stream,
                min_size=args.min_size,
                max_size=args.max_size,
                timeout=args.timeout,
                insecure=args.insecure,
            ))
            exit(0 if success else 1)
    except KeyboardInterrupt:
        print("\nInterrupted by user")
        exit(1)
    except Exception as exc:
        print(f"Error: {exc}")
        import traceback
        traceback.print_exc()
        exit(1)


if __name__ == "__main__":
    main()
