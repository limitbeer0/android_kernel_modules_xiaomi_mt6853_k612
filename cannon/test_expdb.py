#!/usr/bin/env python3
"""Wire-format recovery tests, including RAM and firmware circular wraps."""
import struct
import unittest
import zlib

from read_expdb import (DIRECT_MAGIC, FIRMWARE_WINDOW, NATIVE_HEAD,
                        NATIVE_MAGIC, NATIVE_SIZE, circular, decode,
                        direct_record, native_record)


def native_fixture(count):
    tail_size = NATIVE_SIZE - 64 - NATIVE_HEAD
    stream = (b"0123456789abcdef\n" * ((count + 16) // 17))[:count]
    head_used = min(count, NATIVE_HEAD)
    tail_used = min(count, tail_size)
    cursor = count % tail_size
    ring = bytearray(tail_size)
    for n, value in enumerate(stream[-tail_used:], count - tail_used):
        ring[n % tail_size] = value
    header = struct.pack("<8s14I", NATIVE_MAGIC, 1, NATIVE_SIZE, NATIVE_HEAD,
                         tail_size, head_used, cursor, tail_used, 0, 10,
                         count & 0xffffffff, count >> 32, 0, 0, 0)
    return header + stream[:head_used].ljust(NATIVE_HEAD, b"\0") + ring, stream


class ExpdbTests(unittest.TestCase):
    def test_native_short_has_no_duplicate_prefix(self):
        fixture, expected = native_fixture(1025)
        text, meta = native_record(fixture)
        self.assertEqual(text, expected)
        self.assertEqual(meta["missing_middle_bytes"], 0)

    def test_native_wrap_preserves_head_and_latest_tail(self):
        fixture, expected = native_fixture(900000)
        text, meta = native_record(fixture)
        self.assertTrue(text.startswith(expected[:NATIVE_HEAD]))
        self.assertTrue(text.endswith(expected[-(NATIVE_SIZE - 64 - NATIVE_HEAD):]))
        self.assertGreater(meta["missing_middle_bytes"], 0)

    def test_native_crosses_firmware_window_boundary(self):
        fixture, _ = native_fixture(900000)
        window = bytearray(FIRMWARE_WINDOW - 4096)
        offset = len(window) - 8192
        for i, value in enumerate(fixture):
            window[(offset + i) % len(window)] = value
        image = bytes(8192) + window + bytes(4096)
        results = decode(image)
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0][1]["captured_bytes"], 900000)
        self.assertEqual(results[0][0], native_record(fixture)[0])

    def test_direct_wrap_crc_and_truncation(self):
        capacity = 8 * 1024 * 1024
        count = capacity + 100
        body = bytes(range(256)) * (capacity // 256)
        header = struct.pack("<8s4I4Q2I", DIRECT_MAGIC, 1, 4096, capacity, 0,
                             0x123, count, count + 20, 2, 30, 0)
        header = header[:60] + struct.pack("<I", zlib.crc32(header[:60]))
        image = header.ljust(4096, b"\0") + body
        text, meta = direct_record(image, 0)
        self.assertEqual(text, circular(body, 100, capacity))
        self.assertEqual(meta["cookie"], "0000000000000123")
        with self.assertRaises(ValueError):
            direct_record(image[:-1], 0)
        corrupt = bytearray(image)
        corrupt[56] ^= 1
        with self.assertRaises(ValueError):
            direct_record(corrupt, 0)


if __name__ == "__main__":
    unittest.main()
