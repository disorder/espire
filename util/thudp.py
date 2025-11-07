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


def AES_PADDED_SIZE(len):
    return 16 if (len < 16) else (((len-1) // 16 + 1) * 16)
def HEATING_DGRAM_SIZE(size, secret):
    return AES_PADDED_SIZE(size + len(secret)+1)


class ThUDP:
    # for encrypted (padded)
    # HEATING_DATA_SIZE cmd+name+float+float
    # (1+member_size(heating_t, name)+member_size(heating_t, val)+member_size(heating_t, set))
    name_end = 1+10
    data_end = name_end+4+4

    # all binary arguments
    def __init__(self, secret, key, iv):
        self.secret = secret
        self.key = key
        self.iv = iv
        self.cipher = Cipher(algorithms.AES(key), modes.CBC(iv))

    def prepare(self, cmd, zone, tval, tset):
        # including padding
        size = AES_PADDED_SIZE(HEATING_DGRAM_SIZE(self.data_end, self.secret))
        msg = bytearray(size)

        msg[0] = ord(cmd)
        msg[1:1+len(zone)] = zone.encode('ascii')

        if cmd == '!':
            msg[self.name_end:self.name_end+4] = struct.pack('f', tval)
            msg[self.name_end+4:self.name_end+4+4] = struct.pack('f', tset)
        msg[self.data_end:self.data_end+len(self.secret)+1] = self.secret+b'\x00'

        # zero padded
        #padder = padding.PKCS7(16*8).padder()
        #padded = padder.update(msg) + padder.finalize()
        padded = msg
        enc = self.cipher.encryptor()
        data = enc.update(padded) + enc.finalize()
        return data, bytes(padded)

    def bind(self, ip, port):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        self.sock.bind((ip, port))

    def broadcast(self):
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    def send(self, ip, port, data):
        self.sock.sendto(data, (ip, port))

    def receive(self, validate=True):
        #BUFSIZE = self.data_end + len(self.secret)
        #BUFSIZE = ((BUFSIZE//32) + 1) * 32
        BUFSIZE = AES_PADDED_SIZE(HEATING_DGRAM_SIZE(self.data_end, self.secret))
        renc, addr = self.sock.recvfrom(BUFSIZE)
        dec = self.cipher.decryptor()
        rdec = dec.update(renc) + dec.finalize()
        #unpadder = padding.PKCS7(16*8).unpadder()
        #rdec = unpadder.update(rdec) + unpadder.finalize()
        print('received ', rdec)
        val = struct.unpack('f', rdec[self.name_end:self.name_end+4])
        set = struct.unpack('f', rdec[self.name_end+4:self.name_end+4+4])
        cmd = chr(rdec[0])
        zone = rdec[1:self.name_end].split(b'\0')[0]
        secret = rdec[self.data_end:].split(b'\0')[0]
        if validate and self.secret != secret:
            raise ValueError('invalid secret')
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
        "--broadcast", dest="broadcast", action='store_true', default=False,
    )
    parser.add_argument(
        "--insecure",
        dest="insecure",
        action="store_true",
        default=False,
        help="Do not validate secret",
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
    zone = args.zone

    th = ThUDP(secret, key, iv)
    data, padded = th.prepare(args.type, zone, args.val, args.set)
    print('cleartext', padded)
    print('encrypted', data)
    th.bind(args.bind, args.port)
    if args.broadcast:
        th.broadcast()

    th.send(args.ip, args.port, data)

    # wait for response
    if args.type == '?':
        th.receive(validate=not args.insecure)
    elif args.type == '*':
        while True:
            th.receive(validate=not args.insecure)
