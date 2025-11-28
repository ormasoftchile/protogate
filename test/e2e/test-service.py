#!/usr/bin/env python3
"""
Simple HTTP echo service for E2E testing
Returns request details as JSON
"""

from http.server import HTTPServer, BaseHTTPRequestHandler
import json
import time
import sys

class EchoHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('X-Test-Service', 'true')
        self.end_headers()
        
        response = {
            'echo': 'Hello from test service',
            'method': self.command,
            'path': self.path,
            'headers': dict(self.headers),
            'timestamp': int(time.time())
        }
        
        self.wfile.write(json.dumps(response, indent=2).encode())
    
    def do_POST(self):
        content_length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_length).decode('utf-8') if content_length > 0 else ''
        
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('X-Test-Service', 'true')
        self.end_headers()
        
        response = {
            'echo': 'Hello from test service',
            'method': self.command,
            'path': self.path,
            'headers': dict(self.headers),
            'body': body,
            'timestamp': int(time.time())
        }
        
        self.wfile.write(json.dumps(response, indent=2).encode())
    
    def log_message(self, format, *args):
        # Suppress default logging
        pass

def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
    
    server = HTTPServer(('0.0.0.0', port), EchoHandler)
    print(f'Test service listening on port {port}...')
    
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print('\nShutting down...')
        server.shutdown()

if __name__ == '__main__':
    main()
