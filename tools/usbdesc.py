#!/usr/bin/env python3
"""Hex -> C array / USB descriptor checker.

Input is the hex byte string produced by tshark, for example:

  tshark -r enum.pcapng -Y 'usb.setup.wValue == 0x0100' \
         -T fields -e usb.data_fragment

Accepted separators: spaces, commas, colons, newlines and optional 0x
prefixes.  Do not paste a Wireshark "hex dump" with offsets and ASCII
columns; use the tshark field output instead.

Examples:
  ... | python3 tools/usbdesc.py --c usb_configuration_descriptor
  ... | python3 tools/usbdesc.py --parse
  ... | python3 tools/usbdesc.py --parse --type config
"""

from __future__ import annotations

import argparse
import re
import sys


def parse_hex(text: str) -> bytes:
    # Tokens such as "12", "0x12"; accept "12:34:56", "0x12, 0x34" etc.
    tokens = re.findall(r"(?i)(?<![0-9a-f])(?:0x)?([0-9a-f]{2})(?![0-9a-f])", text)
    if not tokens:
        raise SystemExit("no hex bytes found on stdin")
    return bytes(int(t, 16) for t in tokens)


def u16(data: bytes, off: int) -> int:
    return data[off] | (data[off + 1] << 8)


def c_array(name: str, data: bytes) -> str:
    lines = []
    for i in range(0, len(data), 12):
        chunk = ", ".join(f"0x{b:02X}" for b in data[i : i + 12])
        lines.append(f"    {chunk},")
    body = "\n".join(lines)
    return (
        f"/* {len(data)} bytes */\n"
        f"const uint8_t {name}[{len(data)}] = {{\n"
        f"{body}\n"
        f"}};\n"
    )


def parse_device(data: bytes) -> None:
    if len(data) < 18:
        raise SystemExit(f"device descriptor is only {len(data)} bytes")
    print(f"Device descriptor: {len(data)} bytes")
    print(f"  bLength             {data[0]}")
    print(f"  bDescriptorType     0x{data[1]:02X}")
    print(f"  bcdUSB              0x{u16(data, 2):04X}")
    print(f"  bDeviceClass        0x{data[4]:02X}")
    print(f"  bDeviceSubClass     0x{data[5]:02X}")
    print(f"  bDeviceProtocol     0x{data[6]:02X}")
    print(f"  bMaxPacketSize0     {data[7]}")
    print(f"  idVendor            0x{u16(data, 8):04X}")
    print(f"  idProduct           0x{u16(data, 10):04X}")
    print(f"  bcdDevice           0x{u16(data, 12):04X}")
    print(f"  iManufacturer       {data[14]}")
    print(f"  iProduct            {data[15]}")
    print(f"  iSerialNumber       {data[16]}")
    print(f"  bNumConfigurations  {data[17]}")
    if data[0] != 18:
        print("  ERROR: bLength must be 18")
    if data[1] != 0x01:
        print("  ERROR: bDescriptorType must be 0x01")


def parse_config(data: bytes) -> None:
    if len(data) < 9 or data[1] != 0x02:
        raise SystemExit("not a configuration descriptor")
    total = u16(data, 2)
    print(f"Configuration descriptor: {len(data)} bytes, wTotalLength={total}")
    print(f"  bNumInterfaces      {data[4]}")
    print(f"  bConfigurationValue {data[5]}")
    print(f"  iConfiguration      {data[6]}")
    print(f"  bmAttributes        0x{data[7]:02X}")
    print(f"  bMaxPower           {data[8] * 2} mA")
    if total != len(data):
        print(f"  ERROR: wTotalLength={total}, received {len(data)} bytes")

    p = data[0]
    end = min(total, len(data))
    iface = None
    hid_count = 0
    ep_count = 0

    while p + 2 <= end:
        length = data[p]
        dtype = data[p + 1]
        if length < 2 or p + length > end:
            print(f"  ERROR: bad bLength={length} at offset {p}")
            return
        body = data[p : p + length]

        if dtype == 0x04:
            iface = body[2]
            print(
                f"  [{p:3d}] Interface {iface}: alt={body[3]} "
                f"endpoints={body[4]} class=0x{body[5]:02X} "
                f"subclass=0x{body[6]:02X} protocol=0x{body[7]:02X}"
            )
        elif dtype == 0x21:
            hid_count += 1
            print(
                f"  [{p:3d}] HID: bcdHID=0x{u16(body, 2):04X} "
                f"country={body[4]} descriptors={body[5]} "
                f"type=0x{body[6]:02X} report_len={u16(body, 7)}"
            )
        elif dtype == 0x05:
            ep_count += 1
            attr = body[3] & 0x03
            print(
                f"  [{p:3d}] Endpoint 0x{body[2]:02X}: "
                f"type={'interrupt' if attr == 3 else attr} "
                f"mps={u16(body, 4)} interval={body[6]}"
            )
        elif dtype == 0x0B:
            print(
                f"  [{p:3d}] IAD: first_if={body[2]} count={body[3]} "
                f"class=0x{body[4]:02X} subclass=0x{body[5]:02X} "
                f"protocol=0x{body[6]:02X}"
            )
        else:
            print(f"  [{p:3d}] Descriptor type 0x{dtype:02X}, length {length}")
        p += length

    print(f"  HID descriptors: {hid_count}, endpoints: {ep_count}")
    if p != end:
        print(f"  ERROR: descriptor chain ends at {p}, expected {end}")


def parse_report(data: bytes) -> None:
    print(f"HID report descriptor: {len(data)} bytes")
    print(f"  first bytes: {' '.join(f'{b:02X}' for b in data[:8])}")
    print(f"  last byte:   0x{data[-1]:02X}")
    if data and data[0] == 0x85:
        print("  NOTE: starts with Report ID (0x85); a boot keyboard normally has none")
    if len(data) in (63, 65) and data[0:6] == bytes([0x05, 0x01, 0x09, 0x06, 0xA1, 0x01]):
        print(f"  Known {len(data)}-byte boot-keyboard report descriptor.")
    else:
        print("  Not a known 63/65-byte boot-keyboard descriptor; compare with the capture.")


def parse_string(data: bytes) -> None:
    if len(data) < 2 or data[1] != 0x03:
        raise SystemExit("not a string descriptor")
    print(f"String descriptor: bLength={data[0]}, type=0x{data[1]:02X}")
    if data[0] > len(data):
        print(f"  ERROR: bLength={data[0]} > received {len(data)}")
    if data[0] == 4 and len(data) >= 4:
        print(f"  LangID: 0x{u16(data, 2):04X}")
        return
    raw = data[2 : min(data[0], len(data))]
    if len(raw) % 2:
        raw = raw[:-1]
    try:
        print(f"  text: {raw.decode('utf-16-le')!r}")
    except UnicodeDecodeError:
        print(f"  raw:  {' '.join(f'{b:02X}' for b in raw)}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("file", nargs="?", help="file with hex bytes (default: stdin)")
    ap.add_argument("--c", metavar="NAME", help="emit a C array with this name")
    ap.add_argument("--parse", action="store_true", help="parse the descriptor")
    ap.add_argument(
        "--type",
        choices=["auto", "device", "config", "report", "string"],
        default="auto",
        help="descriptor type for --parse (default: auto)",
    )
    args = ap.parse_args()

    text = open(args.file, encoding="utf-8").read() if args.file else sys.stdin.read()
    data = parse_hex(text)

    if args.c:
        print(c_array(args.c, data), end="")

    if args.parse:
        dtype = args.type
        if dtype == "auto":
            if len(data) >= 2 and data[0] == 0x12 and data[1] == 0x01:
                dtype = "device"
            elif len(data) >= 2 and data[1] == 0x02:
                dtype = "config"
            elif len(data) >= 6 and data[0:2] == b"\x05\x01" and data[2:4] == b"\x09\x06":
                dtype = "report"
            elif len(data) >= 2 and data[1] == 0x03:
                dtype = "string"
            else:
                raise SystemExit("cannot auto-detect descriptor type; use --type")

        if dtype == "device":
            parse_device(data)
        elif dtype == "config":
            parse_config(data)
        elif dtype == "report":
            parse_report(data)
        else:
            parse_string(data)


if __name__ == "__main__":
    main()
