#!/usr/bin/env python3
"""Minimal ALttPO-compatible relay for local co-op testing.

It speaks the classic server protocol 0x02 on UDP: clients send a 20-byte
group name, a message kind and their client index; the relay creates groups on
demand, assigns a per-group client index and forwards broadcasts to the other
clients. It never stores game state.

Usage:
    python3 tools/coop_relay.py [--host 127.0.0.1] [--port 4590]
                                [--verbose] [--dump FILE]

The relay must only be bound to a loopback address for tests. There are no
third-party dependencies.
"""

import argparse
import select
import signal
import socket
import struct
import sys
import time

HEADER = 0x651F
PROTOCOL = 0x02
GROUP_LEN = 20

KIND_REQUEST_INDEX = 0x00
KIND_BROADCAST = 0x01
KIND_BROADCAST_SECTOR = 0x02

REPLY_INDEX = 0x80
REPLY_BROADCAST = 0x81

CLIENT_TIMEOUT = 15.0

PUNCTUATION = set("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~")


def group_key(group):
    text = group.decode("latin-1").lower()
    begin = 0
    end = len(text)
    while begin < end and (text[begin] == " " or text[begin] in PUNCTUATION):
        begin += 1
    while end > begin and (text[end - 1] == " " or text[end - 1] in PUNCTUATION):
        end -= 1
    return text[begin:end]


class Client:
    def __init__(self, index, addr, group):
        self.index = index
        self.addr = addr
        self.group = group
        self.sector = 0
        self.last_seen = time.monotonic()


class Group:
    def __init__(self, key):
        self.key = key
        self.clients = {}

    def next_index(self):
        used = {c.index for c in self.clients.values()}
        index = 0
        while index in used:
            index += 1
        return index


class Relay:
    def __init__(self, host, port, verbose=False, dump=None):
        self.verbose = verbose
        self.dump = dump
        self.groups = {}
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((host, port))
        self.sock.setblocking(False)
        self.running = True
        self.received = 0
        self.forwarded = 0
        if dump:
            self.dump_file = open(dump, "w", encoding="ascii")
        else:
            self.dump_file = None
        print("coop_relay: listening on %s:%d (group timeout %.0fs)"
              % (host, port, CLIENT_TIMEOUT), file=sys.stderr)

    def close(self):
        if self.dump_file:
            self.dump_file.close()
            self.dump_file = None
        self.sock.close()

    def log(self, text):
        if self.verbose:
            print("coop_relay: " + text, file=sys.stderr)

    def find_group(self, key):
        group = self.groups.get(key)
        if group is None:
            group = Group(key)
            self.groups[key] = group
            self.log("created group '%s'" % key)
        return group

    def find_client(self, group, addr):
        client = group.clients.get(addr)
        if client is None:
            index = group.next_index()
            client = Client(index, addr, group)
            group.clients[addr] = client
            self.log("client %s joined '%s' as index %d"
                     % (addr[0] + ":" + str(addr[1]), group.key, index))
        client.last_seen = time.monotonic()
        return client

    def send_packet(self, addr, group_bytes, kind, index, payload):
        packet = struct.pack("<H", HEADER) + bytes([PROTOCOL]) + group_bytes
        packet += bytes([kind])
        packet += struct.pack("<H", index)
        packet += payload
        try:
            self.sock.sendto(packet, addr)
            return True
        except OSError as exc:
            self.log("sendto %s failed: %s" % (addr, exc))
            return False

    def dump_packet(self, addr, data):
        if not self.dump_file:
            return
        self.dump_file.write("%.3f\t%s:%d\t%s\n"
                             % (time.time(), addr[0], addr[1], data.hex()))
        self.dump_file.flush()

    def handle(self, data, addr):
        if len(data) < 2 + 1 + GROUP_LEN + 1 + 2:
            self.log("short packet from %s (%d bytes)" % (addr, len(data)))
            return
        header, protocol = struct.unpack_from("<HB", data, 0)
        if header != HEADER or protocol != PROTOCOL:
            self.log("ignoring packet with header 0x%04X protocol 0x%02X"
                     % (header, protocol))
            return

        pos = 3
        group_bytes = data[pos:pos + GROUP_LEN]
        pos += GROUP_LEN
        kind = data[pos]
        pos += 1
        claimed_index = struct.unpack_from("<H", data, pos)[0]
        pos += 2

        key = group_key(group_bytes)
        group = self.find_group(key)
        client = self.find_client(group, addr)
        self.received += 1
        self.dump_packet(addr, data)

        if kind == KIND_REQUEST_INDEX:
            payload = b""
            if self.send_packet(addr, group_bytes, REPLY_INDEX,
                                client.index, payload):
                self.log("index reply %d to %s:%d"
                         % (client.index, addr[0], addr[1]))

        elif kind == KIND_BROADCAST:
            payload = data[pos:]
            for other in list(group.clients.values()):
                if other.addr == addr:
                    continue
                if time.monotonic() - other.last_seen > CLIENT_TIMEOUT:
                    continue
                if self.send_packet(other.addr, group_bytes, REPLY_BROADCAST,
                                    client.index, payload):
                    self.forwarded += 1
            self.log("broadcast from index %d (%d bytes) to %d client(s)"
                     % (client.index, len(payload),
                        max(0, len(group.clients) - 1)))

        elif kind == KIND_BROADCAST_SECTOR:
            if pos + 4 > len(data):
                self.log("short sector packet from %s" % (addr,))
                return
            sector = struct.unpack_from("<I", data, pos)[0]
            pos += 4
            client.sector = sector
            payload = data[pos:]
            for other in list(group.clients.values()):
                if other.addr == addr or other.sector != sector:
                    continue
                if time.monotonic() - other.last_seen > CLIENT_TIMEOUT:
                    continue
                if self.send_packet(other.addr, group_bytes, REPLY_BROADCAST,
                                    client.index, payload):
                    self.forwarded += 1
            self.log("sector broadcast from index %d sector 0x%04X (%d bytes)"
                     % (client.index, sector & 0xFFFF, len(payload)))
        else:
            self.log("unknown kind 0x%02X from index %d (claimed %d)"
                     % (kind, client.index, claimed_index))

    def cleanup(self):
        now = time.monotonic()
        empty = []
        for key, group in list(self.groups.items()):
            for addr, client in list(group.clients.items()):
                if now - client.last_seen > CLIENT_TIMEOUT:
                    del group.clients[addr]
                    self.log("client %s:%d left '%s' (timeout)"
                             % (addr[0], addr[1], key))
            if not group.clients:
                empty.append(key)
        for key in empty:
            del self.groups[key]
            self.log("group '%s' removed" % key)

    def run(self):
        last_cleanup = time.monotonic()
        while self.running:
            try:
                readable, _, _ = select.select([self.sock], [], [], 0.25)
            except InterruptedError:
                continue
            if readable:
                while True:
                    try:
                        data, addr = self.sock.recvfrom(2048)
                    except BlockingIOError:
                        break
                    except OSError as exc:
                        self.log("recvfrom failed: %s" % (exc,))
                        break
                    self.handle(data, addr)
            now = time.monotonic()
            if now - last_cleanup >= 1.0:
                self.cleanup()
                last_cleanup = now
        self.cleanup()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1",
                        help="bind address (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=4590,
                        help="UDP port (default: 4590)")
    parser.add_argument("--verbose", action="store_true",
                        help="log groups, clients and forwarded packets")
    parser.add_argument("--dump", metavar="FILE",
                        help="write every received client packet as hex")
    args = parser.parse_args()

    if args.port < 1 or args.port > 65535:
        parser.error("--port must be between 1 and 65535")

    relay = Relay(args.host, args.port, args.verbose, args.dump)

    def stop(_signum, _frame):
        relay.running = False

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)
    try:
        relay.run()
    finally:
        print("coop_relay: received %d packet(s), forwarded %d"
              % (relay.received, relay.forwarded), file=sys.stderr)
        relay.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
