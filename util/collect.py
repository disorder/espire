#!/usr/bin/env python3
import socket
import sys
import os
import argparse

parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
parser.add_argument('--port', dest='port', action='store', type=int,
                    required=True,
                    help='Port to listen')
parser.add_argument('--path', dest='path', action='store', default='/tmp',
                    help='Destination for logs')
parser.add_argument('--flush', dest='flush', action='store_true', default=False,
                    help='Flush after each write to file')
parser.add_argument('--tee', dest='tee', action='store_true', default=False,
                    help='Write logs to standard output')
parser.add_argument('--tee-ip', dest='tee_ip', action='store_true', default=False,
                    help='Prepend IP to each received datagram')
parser.add_argument('--limit', dest='limit', action='store', default=2,
                    type=float, help='Limit of rotation in MB, 0 for no limit')
parser.add_argument('--bind', dest='ip', action='store', default='0.0.0.0',
                    help='IP address to bind to')
args = parser.parse_args()
print(args, file=sys.stderr)

# rotate and keep one old logfile
LIMIT = args.limit * 2**20
# won't get more in single UDP
BUFSIZE = 65536

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((args.ip, args.port))
ips = {}
while True:
    data, addr = sock.recvfrom(BUFSIZE)
    ip, _ = addr

    if args.tee:
        if (args.tee_ip):
            sys.stdout.write((ip + ': ').encode('ascii'))
        sys.stdout.buffer.write(data)
        sys.stdout.flush()

    if ip not in ips:
        try:
            ips[ip] = open(os.path.join(args.path, ip+'.log'), 'ab')
        except Exception as exc:
            print(exc)
            continue

    f = ips[ip]
    f.write(data)
    if args.flush:
        f.flush()

    if LIMIT > 0 and f.tell() > LIMIT:
        f.close()
        os.rename(os.path.join(args.path, ip+'.log'), os.path.join(args.path, ip+'.log.old'))
        del ips[ip]
