#!/usr/bin/python

# HTTPS server test application.
#
# You can generate certificate file like this:
#       openssl req -x509 -newkey rsa:2048 -keyout https-server.pem \
#               -out https-server.pem -days 10000 -nodes \
#               -subj '/CN=localhost'
#
# To see the contents of the certificate do this:
#       openssl x509 -in https-server.pem  -text -noout
#
# To add the cert into your application do this:
#       openssl x509 -in https-server.pem  -C -noout
#

import socket
from http.server import HTTPServer
from http.server import SimpleHTTPRequestHandler
import ssl

PORT = 4443

class HTTPServerV6(HTTPServer):
    address_family = socket.AF_INET6

class RequestHandler(SimpleHTTPRequestHandler):
    length = 0

    def _set_headers(self):
        self.send_response(200)
        self.send_header('Content-Type', 'text/html')
        self.send_header('Content-Length', str(self.length))
        self.end_headers()

    def do_POST(self):
        payload = b"<html><p>Done</p></html>"
        self.length = len(payload)
        self._set_headers()
        self.wfile.write(payload)

def main():
    httpd = HTTPServerV6(("", PORT), RequestHandler)
    print("Serving at port", PORT)
    context = ssl.create_default_context(ssl.Purpose.CLIENT_AUTH)
    context.load_cert_chain(certfile='./https-server.pem')
    httpd.socket = context.wrap_socket(httpd.socket, server_side=True)
    httpd.serve_forever()

if __name__ == '__main__':
    main()
