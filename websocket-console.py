#!/usr/bin/env python3
#
# Copyright (c) 2024 Nordic Semiconductor
#
# Connect to Zephyr websocket console and give remote commands
#

import websocket
import threading

def reader(ws):
    while True:
        print(ws.recv().strip("\n"))

if __name__ == "__main__":
    websocket.enableTrace(False)
    ws = websocket.WebSocket()
    ws.connect("ws://192.0.2.1/console")

    x = threading.Thread(target=reader, daemon=True, args=(ws,))
    x.start()

    while True:
        line = input("> ")
        if line == "quit":
            break
        ws.send(line + "\n")

    ws.close()
