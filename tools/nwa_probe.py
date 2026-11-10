#!/usr/bin/env python3
"""Small EmuNWA client for testing the zelda3 SNI bridge.

It follows the detection dialogue used by the real SNI Go client:
EMULATOR_INFO, then the info commands, then CORE_READ with several ranges in
one command. No third-party modules are needed.
"""

import argparse
import socket
import struct
import sys


class NwaError(Exception):
    def __init__(self, kind, reason):
        super().__init__("%s: %s" % (kind, reason))
        self.kind = kind
        self.reason = reason


class NwaConnection:
    def __init__(self, host, port, timeout):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self.buf = b""

    def close(self):
        self.sock.close()

    def _fill(self, need):
        while len(self.buf) < need:
            chunk = self.sock.recv(max(4096, need - len(self.buf)))
            if not chunk:
                raise ConnectionError("connection closed by server")
            self.buf += chunk

    def _read_exact(self, size):
        self._fill(size)
        data, self.buf = self.buf[:size], self.buf[size:]
        return data

    def _read_line(self):
        while b"\n" not in self.buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("connection closed by server")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return line.rstrip(b"\r")

    def _read_ascii(self):
        first = self._read_exact(1)
        if first != b"\n":
            if first == b"\0":
                raise NwaError("protocol_error", "expected ascii reply, got binary")
            raise NwaError("protocol_error", "ascii reply does not start with newline")
        items = []
        item = {}
        while True:
            line = self._read_line()
            if line == b"":
                break
            text = line.decode("utf-8", "replace")
            key, sep, value = text.partition(":")
            if not sep:
                raise NwaError("protocol_error", "reply line without ':'")
            if key in item:
                items.append(item)
                item = {}
            item[key] = value
        if item:
            items.append(item)
        if items and "error" in items[0]:
            raise NwaError(items[0]["error"], items[0].get("reason", ""))
        return items

    def _read_binary(self):
        first = self._read_exact(1)
        if first != b"\0":
            raise NwaError("protocol_error", "binary reply does not start with zero")
        size = struct.unpack(">I", self._read_exact(4))[0]
        return self._read_exact(size)

    def command(self, line, binary=None, want_binary=False):
        payload = line.encode("ascii") + b"\n"
        if binary is not None:
            payload += b"\0" + struct.pack(">I", len(binary)) + binary
        self.sock.sendall(payload)
        if want_binary:
            return self._read_binary()
        return self._read_ascii()


def parse_number(text):
    text = text.strip()
    if not text:
        raise ValueError("empty number")
    if text.startswith("$"):
        return int(text[1:], 16)
    if text.lower().startswith("0x"):
        return int(text, 16)
    return int(text, 10)


def print_ascii(title, items):
    print("%s:" % title)
    for item in items:
        for key, value in item.items():
            print("  %s=%s" % (key, value))


def parse_mem_range(spec):
    parts = spec.split(":")
    if len(parts) != 3:
        raise ValueError("expected MEM:OFFSET:SIZE, got %r" % spec)
    return parts[0], parse_number(parts[1]), parse_number(parts[2])


def read_ranges(conn, memory, ranges):
    line = "CORE_READ %s" % memory
    for offset, size in ranges:
        line += ";$%x;$%x" % (offset, size)
    data = conn.command(line, want_binary=True)
    print("%s: %d bytes" % (line, len(data)))
    pos = 0
    for offset, size in ranges:
        chunk = data[pos:pos + size]
        print("  $%05x..$%05x: %s" % (offset, offset + len(chunk) - 1,
                                      chunk.hex()))
        pos += size
    return data


def decode_wram(data):
    # Default CORE_READ order used below:
    #   0xf340 0x18 items
    #   0xf359 0x03 sword/shield/armor
    #   0xf360 0x08 rupees/compass/bigkey/map
    #   0xf36b 0x10 health/magic/keys/upgrades/pendants
    #   0xf377 0x05 arrows/flags/crystals
    if len(data) < 56:
        return
    items = data[0:24]
    equip = data[24:27]
    rupees = data[27:35]
    status = data[35:51]
    tail = data[51:56]
    print("decoded WRAM fields:")
    print("  link_health_current = %d" % status[2])
    print("  link_health_capacity = %d" % status[1])
    print("  link_magic_power = %d" % status[3])
    print("  link_rupees_actual = %d" % int.from_bytes(rupees[2:4], "little"))
    print("  link_rupees_goal = %d" % int.from_bytes(rupees[0:2], "little"))
    print("  link_num_keys = %d" % status[4])
    print("  link_item_bombs = %d" % items[3])
    print("  link_num_arrows = %d" % tail[0])
    print("  link_bomb_upgrades = %d" % status[5])
    print("  link_arrow_upgrades = %d" % status[6])
    print("  link_sword_type = %d" % equip[0])
    print("  link_shield_type = %d" % equip[1])
    print("  link_armor = %d" % equip[2])
    print("  items[0xf340..0xf357] = %s" % items.hex())
    print("  abilities/crystals = %s" % tail[1:].hex())


def parse_write(spec):
    parts = spec.split(":", 2)
    if len(parts) != 3:
        raise ValueError("expected MEM:OFFSET:HEX, got %r" % spec)
    memory, offset, hexdata = parts
    hexdata = hexdata.strip()
    if hexdata.lower().startswith("0x"):
        hexdata = hexdata[2:]
    hexdata = hexdata.replace(" ", "").replace(":", "")
    if len(hexdata) & 1:
        raise ValueError("HEX must have an even number of digits")
    return memory, parse_number(offset), bytes.fromhex(hexdata)


def do_write(conn, memory, offset, data):
    line = "bCORE_WRITE %s;$%x;$%x" % (memory, offset, len(data))
    try:
        reply = conn.command(line, binary=data)
    except NwaError as exc:
        print("write refused: error=%s reason=%s" % (exc.kind, exc.reason))
        return False
    print("write accepted: %s (%d bytes: %s)" % (line, len(data), data.hex()))
    print_ascii("reply", reply)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=48879)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--name", default="nwa_probe")
    parser.add_argument("--read", action="append", default=[],
                        metavar="MEM:OFFSET:SIZE",
                        help="read one range; may be repeated")
    parser.add_argument("--no-default-read", action="store_true",
                        help="do not read the default WRAM fields")
    parser.add_argument("--write", action="append", default=[],
                        metavar="MEM:OFFSET:HEX",
                        help="bCORE_WRITE hex bytes; may be repeated")
    parser.add_argument("--write-rupees", type=int,
                        help="write link_rupees_actual (WRAM 0xf362)")
    parser.add_argument("--expect-write-refused", action="store_true",
                        help="return success when a write is refused")
    args = parser.parse_args()

    conn = NwaConnection(args.host, args.port, args.timeout)
    try:
        print_ascii("MY_NAME_IS", conn.command("MY_NAME_IS %s" % args.name))
        print_ascii("EMULATOR_INFO", conn.command("EMULATOR_INFO"))
        print_ascii("EMULATION_STATUS", conn.command("EMULATION_STATUS"))
        try:
            print_ascii("GAME_INFO", conn.command("GAME_INFO"))
        except NwaError as exc:
            print("GAME_INFO: error=%s reason=%s" % (exc.kind, exc.reason))
        print_ascii("CORE_CURRENT_INFO", conn.command("CORE_CURRENT_INFO"))
        print_ascii("CORES_LIST", conn.command("CORES_LIST"))
        print_ascii("CORE_MEMORIES", conn.command("CORE_MEMORIES"))

        if args.read:
            for spec in args.read:
                memory, offset, size = parse_mem_range(spec)
                read_ranges(conn, memory, [(offset, size)])
        elif not args.no_default_read:
            ranges = [
                (0xF340, 0x18),  # items
                (0xF359, 0x03),  # sword, shield, armor
                (0xF360, 0x08),  # rupees goal/actual, compass, big key, map
                (0xF36B, 0x10),  # hearts, magic, keys, upgrades, pendants
                (0xF377, 0x05),  # arrows, ability flags, crystals
            ]
            data = read_ranges(conn, "WRAM", ranges)
            decode_wram(data)
            read_ranges(conn, "SRAM", [(0, 16)])
            read_ranges(conn, "CARTROM", [(0, 16)])

        writes = []
        for spec in args.write:
            writes.append(parse_write(spec))
        if args.write_rupees is not None:
            if args.write_rupees < 0 or args.write_rupees > 0xFFFF:
                raise SystemExit("--write-rupees must fit in 16 bits")
            # link_rupees_goal is at 0xf360 and link_rupees_actual at 0xf362.
            # Setting both makes the HUD keep the new value instead of
            # animating actual back towards the old goal.
            writes.append(("WRAM", 0xF360,
                           struct.pack("<HH", args.write_rupees,
                                      args.write_rupees)))

        write_refused = False
        for memory, offset, data in writes:
            if not do_write(conn, memory, offset, data):
                write_refused = True
                continue
            read_ranges(conn, memory, [(offset, len(data))])

        if write_refused and not args.expect_write_refused:
            return 2
    finally:
        conn.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, NwaError, ValueError, ConnectionError) as exc:
        print("nwa_probe: %s" % exc, file=sys.stderr)
        sys.exit(1)
