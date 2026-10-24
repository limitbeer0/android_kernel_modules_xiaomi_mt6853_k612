#!/usr/bin/env python3
"""Decode cannon's opt-in native and direct expdb diagnostic captures.

Read a full partition image. No device access, firmware writes or execution of
log contents. Native PL records can cross its last-2MiB circular window.
"""
import argparse
import json
from pathlib import Path
import struct
import zlib

NATIVE_MAGIC = b"CNL6LOG1"
DIRECT_MAGIC = b"CNL6DIS1"
NATIVE_SIZE = 512 * 1024
NATIVE_HEAD = 128 * 1024
FIRMWARE_WINDOW = 2 * 1024 * 1024


def circular(data, start, size):
    if not data or size > len(data) or not 0 <= start < len(data):
        raise ValueError("invalid circular range")
    first = data[start:start + size]
    return first + data[:size - len(first)]


def native_record(raw):
    if len(raw) != NATIVE_SIZE:
        raise ValueError("truncated native record")
    h = struct.unpack_from("<8s14I", raw)
    magic, version, total, head_size, tail_size = h[:5]
    head_used, tail_next, tail_used, dropped, records, lo, hi = h[5:12]
    count = lo | hi << 32
    if (magic != NATIVE_MAGIC or version != 1 or total != len(raw) or
            head_size != NATIVE_HEAD or 64 + head_size + tail_size != total or
            head_used > head_size or tail_next >= tail_size or tail_used > tail_size or
            head_used != min(count, head_size) or tail_used != min(count, tail_size) or
            tail_next != count % tail_size):
        raise ValueError("invalid native metadata")
    head = raw[64:64 + head_used]
    ring = raw[64 + head_size:]
    tail = circular(ring, (tail_next - tail_used) % tail_size, tail_used)
    tail_start = count - tail_used
    prefix = head[:min(head_used, tail_start)]
    missing = max(0, tail_start - head_used)
    gap = f"\n[CANNON: {missing} middle bytes overwritten]\n".encode() if missing else b""
    return prefix + gap + tail, dict(format="native", captured_bytes=count,
                                    retained_head_bytes=len(prefix), retained_tail_bytes=tail_used,
                                    missing_middle_bytes=missing, dropped_records=dropped,
                                    records=records, buffer_size=total)


def direct_record(image, offset):
    if offset < 0 or offset + 64 > len(image):
        raise ValueError("truncated direct header")
    h = struct.unpack_from("<8s4I4Q2I", image, offset)
    magic, version, header_size, data_size, reserved, cookie, committed, produced, dropped, flushes, crc = h
    if (magic != DIRECT_MAGIC or version != 1 or header_size != 4096 or
            data_size != 8 * 1024 * 1024 or committed > produced or
            offset + header_size + data_size > len(image) or
            zlib.crc32(image[offset:offset + 60]) != crc):
        raise ValueError("invalid direct header, bounds or CRC")
    ring = image[offset + header_size:offset + header_size + data_size]
    size = min(committed, data_size)
    text = circular(ring, (committed - size) % data_size, size)
    return text, dict(format="direct", offset=offset, cookie=f"{cookie:016x}",
                      committed_bytes=committed, produced_bytes=produced,
                      retained_bytes=size, dropped_records=dropped,
                      flushes=flushes, buffer_size=data_size, header_crc_valid=True)


def offsets(data, magic):
    start = 0
    while (start := data.find(magic, start)) >= 0:
        yield start
        start += 1


def decode(image):
    results = []
    for offset in offsets(image, DIRECT_MAGIC):
        try:
            text, meta = direct_record(image, offset)
        except ValueError:
            continue
        results.append((text, meta))
    # PL's data window ends before the final logical-block configuration.
    # cannon UFS has 4096-byte logical sectors; older references use 512.
    # Accept a candidate only when every embedded native cursor validates.
    seen = set()
    if len(image) >= FIRMWARE_WINDOW:
        blocks = [b for b in (4096, 512)
                  if image[-b:-b + 4] == struct.pack("<I", 0x785690ef)]
        for block in blocks or (4096, 512):
            start = len(image) - FIRMWARE_WINDOW
            window = image[start:len(image) - block]
            for relative in offsets(window, NATIVE_MAGIC):
                absolute = start + relative
                if absolute in seen:
                    continue
                try:
                    raw = circular(window, relative, NATIVE_SIZE)
                    text, meta = native_record(raw)
                except ValueError:
                    continue
                meta.update(offset=absolute, firmware_config_block_size=block)
                results.append((text, meta))
                seen.add(absolute)
    return results


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("image", type=Path)
    p.add_argument("output", type=Path)
    args = p.parse_args()
    image = args.image.read_bytes()
    args.output.mkdir(parents=True, exist_ok=True)
    records = decode(image)
    manifest = []
    for index, (text, meta) in enumerate(records):
        name = f"{meta['format']}-{index:02d}-{meta['offset']:08x}.txt"
        # Preserve original bytes; decode only when inspecting as text.
        (args.output / name).write_bytes(text)
        meta["file"] = name
        manifest.append(meta)
    (args.output / "captures.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))
    if not records:
        raise SystemExit("No valid cannon capture found")


if __name__ == "__main__":
    main()
