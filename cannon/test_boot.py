#!/usr/bin/env python3
"""Boot-format regression tests; all synthetic payloads stay in temporary dirs."""
import gzip
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest

from inspect_stock import boot_parts
from pack_boot import entry, pack, read_cpio, write_cpio, unpack_ramdisk, guard_test_fstab


class BootFormatTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.modules = Path(self.temp.name)
        ramdisk = gzip.compress(write_cpio({
            "dev": entry(0o40755), "init": entry(0o100750, b"\x7fELFstock-init"),
            "fstab.mt6853": entry(0o100640, b"original fstab\n"),
            "TRAILER!!!": entry(0),
        }), mtime=0)
        image = bytearray(128)
        image[56:60] = b"ARM\x64"
        image.extend(b"Linux version 6.12.23-cannon-test (test)\0")
        self.kernel = gzip.compress(image, mtime=0)
        self.loader = bytearray(64)
        self.loader[:5] = b"\x7fELF\x02"
        struct.pack_into("<H", self.loader, 18, 183)
        self.dtb = bytes.fromhex("d00dfeed") + bytes(60)
        table = struct.pack(">8I", 0xd7b7ab1e, 128, 32, 32, 1, 32, 2048, 0)
        table += struct.pack(">8I", 64, 64, 0x6853, 7, 1, 2, 3, 4) + self.dtb
        header = bytearray(2048)
        header[:8] = b"ANDROID!"
        struct.pack_into("<10I", header, 8, len(self.kernel), 0x40080000,
                         len(ramdisk), 0x47c80000, 0, 0, 0x4bc80000, 2048, 2, 0x1800017a)
        struct.pack_into("<I", header, 1644, 1660)
        struct.pack_into("<IQ", header, 1648, len(table), 0x4bc80000)
        header[64:69] = b"test\0"
        self.stock = header
        for content in (self.kernel, ramdisk, table):
            self.stock.extend(content)
            self.stock.extend(bytes(-len(self.stock) % 2048))
        module = b"synthetic-unit-test-module"
        (self.modules / "test.ko").write_bytes(module)
        (self.modules / "modules.load").write_text("test.ko\n")
        (self.modules / "manifest.json").write_text(json.dumps({
            "kernel_release": "6.12.23-cannon-test",
            "modules": {"test": {"sha256": hashlib.sha256(module).hexdigest()}},
        }))

    def build(self):
        return pack(self.stock, self.kernel, self.dtb, self.loader, self.modules)

    def test_round_trip_layout_and_ramdisk(self):
        result = self.build()
        info, parts = boot_parts(result)
        self.assertEqual(info["header_version"], 2)
        self.assertEqual(info["ramdisk_addr"], "0x47c80000")
        self.assertEqual(info["cmdline"], "test rdinit=/cannon-init")
        self.assertEqual(parts["kernel"], self.kernel)
        self.assertEqual(parts["dtb"][64:], self.dtb)
        self.assertEqual(struct.unpack_from(">6I", parts["dtb"], 40), (0x6853, 7, 1, 2, 3, 4))
        entries = read_cpio(gzip.decompress(parts["ramdisk"]))
        self.assertEqual(entries["cannon-init"][1], self.loader)
        self.assertEqual(entries["init"][1], b"\x7fELFstock-init")
        self.assertEqual(entries["fstab.mt6853"][0][1], 0o100640)
        self.assertEqual(entries["fstab.mt6853"][1], b"original fstab\n")
        self.assertEqual(entries["lib/modules/modules.load"][1], b"test.ko\n")
        sha = hashlib.sha1()
        for payload in (parts["kernel"], parts["ramdisk"], b"", b"", parts["dtb"]):
            sha.update(payload)
            sha.update(struct.pack("<I", len(payload)))
        self.assertEqual(result[576:596], sha.digest())
        self.assertEqual(len(result) % 2048, 0)

    def test_disable_automatic_filesystem_mutations(self):
        source = (b"# preserve comment\n"
                  b"/dev/block/by-name/metadata /metadata ext4 noatime wait,check,formattable,first_stage_mount\n"
                  b"/dev/block/by-name/userdata /data f2fs inlinecrypt wait,check,formattable,resize,quota\n"
                  b"system /system erofs ro wait,logical,first_stage_mount\n")
        guarded = guard_test_fstab(source)
        self.assertNotIn(b"formattable", guarded)
        self.assertNotIn(b"check", guarded)
        self.assertNotIn(b"resize", guarded)
        self.assertIn(b"wait,first_stage_mount", guarded)
        self.assertIn(b"wait,quota", guarded)
        self.assertIn(b"system /system erofs ro wait,logical,first_stage_mount", guarded)
        self.assertTrue(guarded.startswith(b"# preserve comment\n"))
        self.assertEqual(guard_test_fstab(guarded), guarded)

    def test_reject_changed_module(self):
        (self.modules / "test.ko").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "changed"):
            self.build()

    def test_crypto_mode_keeps_original_init_and_reader(self):
        result = pack(self.stock, self.kernel, self.dtb, self.loader, self.modules,
                      crypto_probe=self.loader)
        _, parts = boot_parts(result)
        _, cpio = unpack_ramdisk(parts["ramdisk"])
        entries = read_cpio(cpio)
        self.assertEqual(entries["cannon-watchdog-test"][1], b"F")
        self.assertEqual(entries["cannon-fscrypt-probe"][1], self.loader)
        self.assertEqual(entries["cannon-fscrypt-probe"][0][1], 0o100750)
        self.assertEqual(entries["init"][1], b"\x7fELFstock-init")
        with self.assertRaisesRegex(ValueError, "mutually exclusive"):
            pack(self.stock, self.kernel, self.dtb, self.loader, self.modules,
                 watchdog_android=True, crypto_probe=self.loader)

    def test_crypto_reader_rejects_dynamic_linker(self):
        probe = bytearray(self.loader) + bytes(56)
        struct.pack_into("<Q", probe, 32, 64)
        struct.pack_into("<HH", probe, 54, 56, 1)
        struct.pack_into("<I", probe, 64, 3)  # PT_INTERP
        with self.assertRaisesRegex(ValueError, "must be static"):
            pack(self.stock, self.kernel, self.dtb, self.loader, self.modules,
                 crypto_probe=probe)

    def test_long_android_watchdog_mode_is_explicit(self):
        result = pack(self.stock, self.kernel, self.dtb, self.loader, self.modules,
                      watchdog_android_long=True)
        _, parts = boot_parts(result)
        _, cpio = unpack_ramdisk(parts["ramdisk"])
        entries = read_cpio(cpio)
        self.assertEqual(entries["cannon-watchdog-test"][1], b"L")
        self.assertEqual(entries["init"][1], b"\x7fELFstock-init")
        with self.assertRaisesRegex(ValueError, "mutually exclusive"):
            pack(self.stock, self.kernel, self.dtb, self.loader, self.modules,
                 watchdog_android=True, watchdog_android_long=True)

    def test_native_expdb_needs_bounded_android_diagnostic(self):
        with self.assertRaisesRegex(ValueError, "requires the Android watchdog"):
            pack(self.stock, self.kernel, self.dtb, self.loader, self.modules,
                 native_expdb=True)
        result = pack(self.stock, self.kernel, self.dtb, self.loader, self.modules,
                      watchdog_android=True, native_expdb=True)
        info, parts = boot_parts(result)
        self.assertIn("cannon.log_store=1", info["cmdline"].split())
        self.assertIn("cannon.expdb=1", info["cmdline"].split())
        self.assertIn("mtk_wdt.cannon_budget=600", info["cmdline"].split())
        self.assertIn("cannon.expdb_seconds=660", info["cmdline"].split())
        _, cpio = unpack_ramdisk(parts["ramdisk"])
        self.assertEqual(read_cpio(cpio)["cannon-watchdog-test"][1], b"A")
        self.assertNotIn("cannon.log_store=1", boot_parts(self.build())[0]["cmdline"])

    def test_adbroot_is_explicit_and_checks_kernel_support(self):
        with self.assertRaisesRegex(ValueError, "requires the bounded"):
            pack(self.stock, self.kernel, self.dtb, self.loader, self.modules, adb_root=True)
        with self.assertRaisesRegex(ValueError, "lacks the cannon adbroot"):
            pack(self.stock, self.kernel, self.dtb, self.loader, self.modules,
                 watchdog_android=True, native_expdb=True, adb_root=True)
        kernel = gzip.compress(gzip.decompress(self.kernel) + b"cannon-adbroot: authenticated\0")
        result = pack(self.stock, kernel, self.dtb, self.loader, self.modules,
                      watchdog_android=True, native_expdb=True, adb_root=True)
        self.assertIn("cannon.adbroot=1", boot_parts(result)[0]["cmdline"].split())
        self.assertNotIn("cannon.adbroot=1", boot_parts(self.build())[0]["cmdline"])

    def test_crypto_write_mode_is_explicit_and_requires_reader(self):
        result = pack(self.stock, self.kernel, self.dtb, self.loader, self.modules,
                      crypto_probe=self.loader, crypto_write=True)
        _, parts = boot_parts(result)
        _, cpio = unpack_ramdisk(parts["ramdisk"])
        entries = read_cpio(cpio)
        self.assertEqual(entries["cannon-watchdog-test"][1], b"G")
        self.assertEqual(entries["init"][1], b"\x7fELFstock-init")
        with self.assertRaisesRegex(ValueError, "needs a crypto probe"):
            pack(self.stock, self.kernel, self.dtb, self.loader, self.modules,
                 crypto_write=True)

    def test_reject_kernel_module_release_mismatch(self):
        manifest = json.loads((self.modules / "manifest.json").read_text())
        manifest["kernel_release"] = "4.14.336"
        (self.modules / "manifest.json").write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "release differs"):
            self.build()

    def test_reject_duplicate_load(self):
        (self.modules / "modules.load").write_text("test.ko\ntest.ko\n")
        with self.assertRaisesRegex(ValueError, "duplicate"):
            self.build()

    def test_reject_path_escape(self):
        (self.modules / "modules.load").write_text("../test.ko\n")
        with self.assertRaisesRegex(ValueError, "invalid module path"):
            self.build()

    def test_reject_truncated_boot(self):
        with self.assertRaisesRegex(ValueError, "truncated"):
            boot_parts(self.stock[:2048])

    def test_raw_ramdisk_and_appended_dtb(self):
        _, parts = boot_parts(self.stock)
        raw = gzip.decompress(parts["ramdisk"])
        base_dtb = bytearray(self.dtb)
        struct.pack_into(">I", base_dtb, 4, len(base_dtb))
        base_kernel = parts["kernel"] + base_dtb
        header = bytearray(self.stock[:2048])
        struct.pack_into("<I", header, 8, len(base_kernel))
        struct.pack_into("<I", header, 16, len(raw))
        self.stock = header
        for payload in (base_kernel, raw, parts["dtb"]):
            self.stock.extend(payload)
            self.stock.extend(bytes(-len(self.stock) % 2048))
        _, result = boot_parts(self.build())
        self.assertEqual(result["kernel"], self.kernel + self.dtb)
        compression, cpio = unpack_ramdisk(result["ramdisk"])
        self.assertEqual(compression, "none")
        self.assertEqual(read_cpio(cpio)["init"][1], b"\x7fELFstock-init")


if __name__ == "__main__":
    unittest.main()
