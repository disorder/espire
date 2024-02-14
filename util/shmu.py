#!/usr/bin/env python3
from http.server import HTTPServer, BaseHTTPRequestHandler
from socketserver import ThreadingMixIn
from socketserver import TCPServer, BaseRequestHandler
import urllib.request
from datetime import datetime, timedelta
import pytz
from subprocess import Popen, PIPE

import sys
import argparse

parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
parser.add_argument('--port', dest='port', action='store', type=int,
                    required=True,
                    help='Port to listen')
parser.add_argument('--no-swap', dest='swap', action='store_false', default=True,
                    help='Swap bytes to big endian order')
parser.add_argument('--reverse', dest='reverse', action='store_true', default=False,
                    help='Reverse Y axis')
parser.add_argument('--bmp', dest='bmp', action='store_true', default=False,
                    help='Keep BMP header (for debugging)')
parser.add_argument('--http', dest='http', action='store_true', default=False,
                    help='Use HTTP')
parser.add_argument('--bind', dest='ip', action='store', default='0.0.0.0',
                    help='IP address to bind to')

# HTTP is not optimal for memory
class SHMU_HTTP(BaseHTTPRequestHandler):
    def do_GET(self):
        path =  self.path.strip('/').split('.')[0]
        if path in ('radar', 'eumssk', 'eumseu'):
            getattr(self.shmu, path)(self)
        else:
            self.send_response(404)
            self.end_headers()
            return
        return

    def write(self, data):
        self.wfile.write(data)

class SHMU_TCP(BaseRequestHandler):
    def handle(self):
        path = self.request.recv(10).decode('ascii', 'ignore').strip()
        if path in ('radar', 'eumssk', 'eumseu'):
            getattr(self.shmu, path)(self)
        return

    def send_header(self, key, value): pass
    def end_headers(self): pass
    def send_response(self, code): pass

    def write(self, data): self.request.send(data)
    #def close(self): self.request.close()

class SHMU:
    def __init__(self, swap, reverse, bmp):
        self.swap = swap
        self.reverse = reverse
        self.bmp = bmp

    def time(self, m):
        now = datetime.now(pytz.UTC)
        return (now - timedelta(minutes=now.minute % m)).replace(second=0, microsecond=0)

    cache = {}
    def download(self, name, url, m):
        last = self.time(m)
        delta = timedelta(minutes=m)
        citem = self.cache.get(name, (None, None, 0))
        clast, cdata, cached = citem
        for x in range(10):
            if clast == last:
                print('cached', clast)
                return citem
            try:
                last_url = last.strftime(url)
                #print(last)
                #print(last_url)
                with urllib.request.urlopen(last_url) as f:
                    citem = [last, f.read(), 0]
                    self.cache[name] = citem
                    return citem
            except Exception as exc:
                print(exc)
            last -= delta
        return None, None

    def radar_cmd(self, last):
        return [
            'convert', '-',
            '-crop', '590x295+6+55', '-resize', '160x80',
            '-font', 'Terminus', '-fill', 'white',
            #'-pointsize', '6', '-annotate', '+112+93',
            #'-pointsize', '8', '-annotate', '+75+93',
            '-pointsize', '12', '-annotate', '+50+93',
            last.strftime('%Y-%m-%d %H:%M'),
            # for PNG
            '+matte',
            '-type', 'TrueColor', '-depth', '16',
            '-define', 'bmp:subtype=RGB565', 'bmp:-'
        ]

    def eumssk_cmd(self, last):
        return [
            'convert', '-',
            '-crop', '320x160+17+313', '-resize', '160x80',
            '-font', 'Terminus', '-pointsize', '8', '-fill', 'white',
            #'-pointsize', '8', '-annotate', '+82+233',
            '-pointsize', '12', '-annotate', '+57+233',
            last.strftime('%Y-%m-%d %H:%M'),
            '-type', 'TrueColor', '-depth', '16',
            '-define', 'bmp:subtype=RGB565', 'bmp:-'
        ]

    def eumseu_cmd(self, last):
        return [
            'convert', '-',
            '-crop', '160x80+366+253', '+repage',
            '-font', 'Terminus', '-fill', 'white',
            #'-pointsize', '8', '-annotate', '+74+78',
            '-pointsize', '12', '-annotate', '+49+78',
            last.strftime('%Y-%m-%d %H:%M'),
            '-type', 'TrueColor', '-depth', '16',
            '-define', 'bmp:subtype=RGB565', 'bmp:-'
        ]

    radar_url = 'https://www.shmu.sk/data/data002/radar-cappi_z_2_600x480-%Y%m%d-%H%M-mosaic--.png'
    eumssk_url = 'https://www.shmu.sk/data/datadruzice/147/img-147-%Y%m%d-%H%M-eums-.jpg'
    eumseu_url = 'https://www.shmu.sk/data/datadruzice/003/img-003-%Y%m%d-%H%M-eums-.jpg'

    def exec(self, server, name, url, cmd, m):
        w = 160
        line_w = w*2
        h = 80
        last, data, cached = self.download(name, url, m)
        if data is None:
            server.send_response(404)
        else:
            server.send_response(200)
            # better to write it into image
            #server.send_header('Date', last.strftime('%H:%M'))
            server.send_header('Content-Length', line_w*h + (138 if self.bmp else 0))
        server.end_headers()
        if cached:
            server.write(data)
        elif data is not None:
            #server.write(data)
            proc = Popen(cmd(last), stdin=PIPE, stdout=PIPE, bufsize=-1)
            proc.stdin.write(data)
            proc.stdin.close()
            proc.wait()
            data = proc.stdout.read()
            i = 138
            cdata = bytearray()
            if self.bmp:
                #server.write(data[0:i])
                cdata += data[0:i]

            if self.swap:
                data = bytearray(data[i:])
                for x in range(len(data)//2):
                    data[x*2], data[x*2+1] = data[x*2+1], data[x*2]
            else:
                data = data[i:]

            y = h-1 if self.reverse else 0
            i = 0
            while i < len(data):
                y_idx = y*line_w
                line = data[y_idx:y_idx+line_w]
                # if self.swap:
                #     for x in range(w):
                #         server.write(line[x*2+1].to_bytes(1, byteorder='big'))
                #         server.write(line[x*2].to_bytes(1, byteorder='big'))
                cdata += line
                i += line_w
                if self.reverse:
                    y -= 1
                else:
                    y += 1
            self.cache[name][1] = cdata
            self.cache[name][2] = 1
            server.write(cdata)
            #server.close()

    def radar(self, server):
        self.exec(server, 'radar', self.radar_url, self.radar_cmd, 5)

    def eumssk(self, server):
        self.exec(server, 'eumssk', self.eumssk_url, self.eumssk_cmd, 15)

    def eumseu(self, server):
        self.exec(server, 'eumseu', self.eumseu_url, self.eumseu_cmd, 15)


class ThreadedHTTPServer(ThreadingMixIn, HTTPServer):
    """Handle requests in a separate thread."""

class ThreadedTCPServer(ThreadingMixIn, TCPServer):
    """Handle requests in a separate thread."""

if __name__ == '__main__':
    args = parser.parse_args()
    print(args, file=sys.stderr)
    shmu = SHMU(swap=args.swap, reverse=args.reverse, bmp=args.bmp)

    if args.http:
        handler = SHMU_HTTP
        server = ThreadedHTTPServer((args.ip, args.port), handler)
    else:
        handler = SHMU_TCP
        server = ThreadedTCPServer((args.ip, args.port), handler)

    server.allow_reuse_address = True
    handler.shmu = shmu
    try:
        server.serve_forever()
    except Exception as exc:
        print(exc);
        server.server_close(self)
