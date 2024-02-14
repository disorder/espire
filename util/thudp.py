#!/usr/bin/env python3
# utility for sending encrypted requests and decoding responses

import os
import sys
import argparse
import socket
import struct
import base64
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives import padding

parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
parser.add_argument('--port', dest='port', action='store', type=int,
                    default=1024,
                    help='Port to send/listen')
parser.add_argument('--bind', dest='bind', action='store', default='0.0.0.0',
                    help='IP address to bind to')
parser.add_argument('--ip', dest='ip', action='store', required=True,
                    help='IP address to send to')
parser.add_argument('--type', dest='type', action='store', required=True,
                    help='Message type: ?#!')
parser.add_argument('--zone', dest='zone', action='store', default='',
                    help='Zone name')
parser.add_argument('--secret', dest='secret', action='store',
                    help='Secret (loads UDP_SECRET)')
parser.add_argument('--temp-val', dest='val', action='store', type=float, default=float('nan'),
                    help='Temperature value')
parser.add_argument('--temp-set', dest='set', action='store', type=float, default=float('nan'),
                    help='Desired temperature value')
parser.add_argument('--key', dest='key', action='store',
                    default=base64.b64encode(b"12345678901234567890123456789012"),
                    help='Key (loads UDP_KEY env var)')
parser.add_argument('--iv', dest='iv', action='store',
                    default=base64.b64encode(b"1234567890123456"),
                    help='IV (loads UDP_IV env var)')
args = parser.parse_args()
print(args, file=sys.stderr)
# without base64 it can only be text

args.key = os.environ.get('UDP_KEY', args.key)
args.iv = os.environ.get('UDP_IV', args.iv)
if args.secret is None:
    args.secret = os.environ['UDP_SECRET']

secret = args.secret.encode('ascii')
key = base64.b64decode(args.key)
iv = base64.b64decode(args.iv)
zone = args.zone.encode('ascii')
cipher = Cipher(algorithms.AES(key), modes.CBC(iv))

name_end = 1+10
data_end = name_end+4+4
msg = bytearray(data_end)
msg[0] = ord(args.type[0])
msg[1:1+len(zone)] = zone
if args.type == '!':
    msg[name_end:name_end+4] = struct.pack('f', args.val)
    msg[name_end+4:name_end+4+4] = struct.pack('f', args.set)
msg[data_end:data_end+len(secret)] = secret+b'\x00'

padder = padding.PKCS7(16*8).padder() 
padded = padder.update(msg) + padder.finalize()
enc = cipher.encryptor()
data = enc.update(padded) + enc.finalize()

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((args.bind, args.port))
ips = {}
BUFSIZE=len(data)
dec = cipher.decryptor()
print('cleartext', padded)
print('encrypted', data)
sock.sendto(data, (args.ip, args.port))

def receive():
    renc, addr = sock.recvfrom(BUFSIZE)
    print(addr)
    rdec = dec.update(renc) + dec.finalize()
    print('received ', rdec)
    val = struct.unpack('f', rdec[name_end:name_end+4])
    set = struct.unpack('f', rdec[name_end+4:name_end+4+4])
    print(chr(rdec[0]), rdec[1:name_end].split(b'\0')[0], val, set, rdec[data_end:].split(b'\0')[0])

# wait for response
if args.type == '?':
    receive()
elif args.type == '*':
    while True:
        receive()