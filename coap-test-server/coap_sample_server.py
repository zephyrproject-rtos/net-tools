#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025 Netfeasa Ltd.

import asyncio
import logging

import aiocoap.resource as resource
from aiocoap import Code, Message
from aiocoap.numbers import ContentFormat

logging.basicConfig(level=logging.INFO)
LOGGER = logging.getLogger("coap-zephyr-test-server")

BLOCK_WISE_TRANSFER_SIZE_GET = 2048


def _request_type_name(request: Message) -> str:
    return str(request.mtype) if request.mtype is not None else "unknown"


class TestResource(resource.Resource):
    async def render_get(self, request: Message) -> Message:
        payload = (
            f"Type: {_request_type_name(request)}\nCode: {request.code}\nMID: {request.mid}\n"
        ).encode()
        return Message(
            code=Code.CONTENT,
            payload=payload,
            content_format=ContentFormat.TEXT,
        )

    async def render_put(self, request: Message) -> Message:
        LOGGER.info("PUT /test payload=%r", request.payload)
        return Message(code=Code.CHANGED)

    async def render_post(self, request: Message) -> Message:
        LOGGER.info("POST /test payload=%r", request.payload)
        return Message(
            code=Code.CREATED,
            location_path=("location1", "location2", "location3"),
        )

    async def render_delete(self, request: Message) -> Message:
        return Message(code=Code.DELETED)


class LargeResource(resource.Resource):
    async def render_get(self, request: Message) -> Message:
        payload = b"A" * BLOCK_WISE_TRANSFER_SIZE_GET
        return Message(
            code=Code.CONTENT,
            payload=payload,
            content_format=ContentFormat.TEXT,
        )


class ObsResource(resource.ObservableResource):
    def __init__(self) -> None:
        super().__init__()
        self.counter = 0
        self._task: asyncio.Task | None = asyncio.create_task(self._ticker())

    async def _ticker(self) -> None:
        while True:
            await asyncio.sleep(5)
            self.counter += 1
            LOGGER.info("/obs counter=%d", self.counter)
            self.updated_state()

    async def render_get(self, request: Message) -> Message:
        payload = f"Counter: {self.counter}\n".encode()
        return Message(
            code=Code.CONTENT,
            payload=payload,
            content_format=ContentFormat.TEXT,
        )


async def main() -> None:
    root = resource.Site()
    root.add_resource(["test"], TestResource())
    root.add_resource(["large"], LargeResource())
    root.add_resource(["obs"], ObsResource())

    await aiocoap.Context.create_server_context(root, bind=("0.0.0.0", 5683))

    LOGGER.info("CoAP server ready on udp/5683")
    LOGGER.info("Resources: /test, /large, /obs")

    await asyncio.get_running_loop().create_future()


if __name__ == "__main__":
    import aiocoap

    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        LOGGER.info("Server stopped")
