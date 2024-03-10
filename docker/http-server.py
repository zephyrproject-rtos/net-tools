#!/usr/bin/env python3

import socket
from http.server import HTTPServer, BaseHTTPRequestHandler

PORT = 8000

class HTTPServerV6(HTTPServer):
    address_family = socket.AF_INET6

class RequestHandler(BaseHTTPRequestHandler):
    length = 0

    def _set_headers(self):
        self.send_response(200)
        self.send_header('Content-Type', 'text/html')
        self.send_header('Content-Length', str(self.length))
        self.end_headers()

    def do_POST(self):
        payload = "<html><p>Done</p></html>"
        self.length = len(payload)
        self._set_headers()
        self.wfile.write(payload.encode())

    def do_GET(self):
        payload = "<html><p>Done</p></html>"
        self.length = len(payload)
        self._set_headers()
        self.wfile.write(payload.encode())

def main():
    httpd = HTTPServerV6(("", PORT), RequestHandler)
    print("Serving at port", PORT)
    httpd.serve_forever()

if __name__ == '__main__':
    main()
