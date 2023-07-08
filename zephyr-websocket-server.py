#!/usr/bin/python

import sys
from os.path import dirname
sys.path.append(dirname(__file__) + "/python-websocket-server")

from websocket_server import WebsocketServer
import logging

# Called for every client connecting (after handshake)
def new_client(client, server):
	print("New client connected and was given id %d" % client['id'])
	#server.send_message_to_all("Hey all, a new client has joined us")

# Called for every client disconnecting
def client_left(client, server):
	print("Client(%d) disconnected" % client['id'])

# Called when a client sends a message
def message_received(client, server, message):
	print("Client(%d) sent[%d]" % (client['id'], len(message)))
	server.send_message(client, message)

PORT=9001
server = WebsocketServer(PORT, host="192.0.2.2", loglevel=logging.INFO)
server.set_fn_new_client(new_client)
server.set_fn_client_left(client_left)
server.set_fn_message_received(message_received)
server.run_forever()
