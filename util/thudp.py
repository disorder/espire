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


class ThUDP:
    name_end = 1+10
    data_end = name_end+4+4

    # all binary arguments
    def __init__(self, secret, key, iv):
        self.secret = secret
        self.key = key
        self.iv = iv
        self.cipher = Cipher(algorithms.AES(key), modes.CBC(iv))

    def prepare(self, cmd, zone, tval, tset):
        msg = bytearray(self.data_end)

        msg[0] = ord(args.type[0])
        msg[1:1+len(zone)] = zone

        if cmd == '!':
            msg[self.name_end:self.name_end+4] = struct.pack('f', tval)
            msg[self.name_end+4:self.name_end+4+4] = struct.pack('f', tset)
        msg[self.data_end:self.data_end+len(secret)] = secret+b'\x00'

        padder = padding.PKCS7(16*8).padder()
        padded = padder.update(msg) + padder.finalize()
        enc = self.cipher.encryptor()
        data = enc.update(padded) + enc.finalize()
        self.BUFSIZE = len(data)
        return data, padded

    def bind(self, ip, port):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((ip, port))

    def send(self, ip, port, data):
        self.sock.sendto(data, (ip, port))

    def receive(self):
        renc, addr = self.sock.recvfrom(self.BUFSIZE)
        print(addr)
        dec = self.cipher.decryptor()
        rdec = dec.update(renc) + dec.finalize()
        print('received ', rdec)
        val = struct.unpack('f', rdec[self.name_end:self.name_end+4])
        set = struct.unpack('f', rdec[self.name_end+4:self.name_end+4+4])
        cmd = chr(rdec[0])
        zone = rdec[1:self.name_end].split(b'\0')[0]
        # TODO compare secret
        secret = rdec[self.data_end:].split(b'\0')[0]
        print(cmd, zone, val, set, secret)
        return cmd, zone, val, set, secret


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    parser.add_argument(
        "--port",
        dest="port",
        action="store",
        type=int,
        default=1024,
        help="Port to send/listen",
    )
    parser.add_argument(
        "--bind",
        dest="bind",
        action="store",
        default="0.0.0.0",
        help="IP address to bind to",
    )
    parser.add_argument(
        "--ip", dest="ip", action="store", required=True, help="IP address to send to"
    )
    parser.add_argument(
        "--type", dest="type", action="store", required=True, help="Message type: *?#!"
    )
    parser.add_argument(
        "--zone", dest="zone", action="store", default="", help="Zone name"
    )
    parser.add_argument(
        "--secret", dest="secret", action="store", help="Secret (loads UDP_SECRET)"
    )
    parser.add_argument(
        "--temp-val",
        dest="val",
        action="store",
        type=float,
        default=float("nan"),
        help="Temperature value",
    )
    parser.add_argument(
        "--temp-set",
        dest="set",
        action="store",
        type=float,
        default=float("nan"),
        help="Desired temperature value",
    )
    parser.add_argument(
        "--key",
        dest="key",
        action="store",
        default=base64.b64encode(b"12345678901234567890123456789012"),
        help="Key (loads UDP_KEY env var)",
    )
    parser.add_argument(
        "--iv",
        dest="iv",
        action="store",
        default=base64.b64encode(b"1234567890123456"),
        help="IV (loads UDP_IV env var)",
    )
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

    th = ThUDP(secret, key, iv)
    data, padded = th.prepare(args.type, zone, args.val, args.set)
    print('cleartext', padded)
    print('encrypted', data)
    th.bind(args.bind, args.port)

    th.send(args.ip, args.port, data)

    # wait for response
    if args.type == '?':
        th.receive()
    elif args.type == '*':
        while True:
            th.receive()
