#!/usr/bin/env python3
#
# Copyright (c) 2026 Nordic Semiconductor
# SPDX-License-Identifier: Apache-2.0
#
import argparse
import asyncio
import logging
import random
import time
from dataclasses import dataclass, field
from typing import Dict

from aioquic.asyncio import serve
from aioquic.asyncio.protocol import QuicConnectionProtocol
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.events import StreamDataReceived, ConnectionTerminated


# -------------------------
# Per-stream statistics
# -------------------------

@dataclass
class StreamStats:
    stream_id: int
    bytes_received: int = 0
    bytes_sent: int = 0
    start_time: float = field(default_factory=time.time)
    end_time: float | None = None

    def finish(self):
        self.end_time = time.time()

    @property
    def duration(self):
        if self.end_time is None:
            return None
        return self.end_time - self.start_time


# -------------------------
# Echo protocol
# -------------------------

class EchoServerProtocol(QuicConnectionProtocol):
    def __init__(self, *args, loss_rate=0.0, delay_ms=0, jitter_ms=0, **kwargs):
        super().__init__(*args, **kwargs)
        self.loss_rate = loss_rate
        self.delay_ms = delay_ms
        self.jitter_ms = jitter_ms
        self.stream_stats: Dict[int, StreamStats] = {}

    async def _echo_with_impairment(self, stream_id: int, data: bytes, end_stream: bool):
        # Simulated loss
        if self.loss_rate > 0 and random.random() < self.loss_rate:
            logging.debug("Dropping %d bytes on stream %d", len(data), stream_id)
            return

        # Simulated delay + jitter
        delay = self.delay_ms / 1000.0
        if self.jitter_ms > 0:
            delay += random.uniform(0, self.jitter_ms / 1000.0)

        if delay > 0:
            await asyncio.sleep(delay)

        self._quic.send_stream_data(
            stream_id,
            data,
            end_stream=end_stream,
        )
        self.transmit()

        stats = self.stream_stats[stream_id]
        stats.bytes_sent += len(data)

    def quic_event_received(self, event):
        if isinstance(event, StreamDataReceived):
            sid = event.stream_id

            if sid not in self.stream_stats:
                self.stream_stats[sid] = StreamStats(stream_id=sid)

            stats = self.stream_stats[sid]
            stats.bytes_received += len(event.data)

            logging.debug(
                "Stream %d: recv=%d end=%s",
                sid,
                len(event.data),
                event.end_stream,
            )

            # Echo asynchronously (so we can delay/drop)
            if event.data or event.end_stream:
                asyncio.create_task(
                    self._echo_with_impairment(
                        sid,
                        event.data,
                        event.end_stream,
                    )
                )

            if event.end_stream:
                stats.finish()

        elif isinstance(event, ConnectionTerminated):
            logging.info("Connection terminated")
            self.dump_stats()

    def dump_stats(self):
        logging.info("==== Stream statistics ====")
        for sid, s in sorted(self.stream_stats.items()):
            logging.info(
                "Stream %d: recv=%d sent=%d duration=%.3fs",
                sid,
                s.bytes_received,
                s.bytes_sent,
                s.duration or 0.0,
            )
        logging.info("===========================")


# -------------------------
# Main
# -------------------------

async def main():
    p = argparse.ArgumentParser("Advanced QUIC echo server")
    p.add_argument("--host", default="0.0.0.0")
    p.add_argument("--port", type=int, default=4422)
    p.add_argument("--certificate", required=True)
    p.add_argument("--private-key", required=True)

    # Flow control
    p.add_argument("--max-data", type=int, default=64 * 1024,
                   help="Connection-level flow control window")
    p.add_argument("--max-stream-data", type=int, default=16 * 1024,
                   help="Per-stream flow control window")
    p.add_argument("--max-streams", type=int, default=100)

    # Impairments
    p.add_argument("--loss-rate", type=float, default=0.0)
    p.add_argument("--delay-ms", type=int, default=0)
    p.add_argument("--jitter-ms", type=int, default=0)

    p.add_argument("-d", "--debug", type=int, default=0, choices=[0, 1, 2])
    args = p.parse_args()

    logging.basicConfig(
        level=[logging.WARNING, logging.INFO, logging.DEBUG][args.debug]
    )

    logging.getLogger("quic").setLevel(
        level=[logging.WARNING, logging.INFO, logging.DEBUG][args.debug]
    )

    config = QuicConfiguration(
        is_client=False,
        alpn_protocols=["echo-quic"],
    )
    config.load_cert_chain(args.certificate, args.private_key)

    # Flow control tuning
    config.max_data = args.max_data
    config.max_stream_data_bidi_local = args.max_stream_data
    config.max_stream_data_bidi_remote = args.max_stream_data
    config.max_streams_bidi = args.max_streams

    await serve(
        args.host,
        args.port,
        configuration=config,
        create_protocol=lambda *a, **kw: EchoServerProtocol(
            *a,
            loss_rate=args.loss_rate,
            delay_ms=args.delay_ms,
            jitter_ms=args.jitter_ms,
            **kw,
        ),
    )

    logging.info(
        "Listening on %s:%d (loss=%.2f delay=%dms jitter=%dms)",
        args.host,
        args.port,
        args.loss_rate,
        args.delay_ms,
        args.jitter_ms,
    )

    await asyncio.Future()


if __name__ == "__main__":
    asyncio.run(main())
