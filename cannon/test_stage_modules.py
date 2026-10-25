#!/usr/bin/env python3
"""Exercise missing/cyclic dependencies, stale outputs, and module ordering."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import stage_modules


class StagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.kernel = self.base / "kernel"
        self.external = self.base / "external"
        self.dest = self.base / "stage"
        (self.kernel / "include/config").mkdir(parents=True)
        self.external.mkdir()
        (self.kernel / "include/config/kernel.release").write_text("6.12.23-test\n")
        (self.kernel / "Module.symvers").write_text("test export set\n")
        (self.kernel / "System.map").write_text("test map\n")
        (self.kernel / "modules.builtin").write_text("kernel/builtin.ko\n")
        (self.kernel / "modules.order").write_text("provider.o\n")
        (self.external / "modules.order").write_text(str(self.external / "consumer.o") + "\n")
        (self.kernel / "provider.ko").write_bytes(b"provider test fixture")
        (self.external / "consumer.ko").write_bytes(b"consumer test fixture")
        # Must never be found by a recursive *.ko walk.
        (self.external / "stale.ko").write_bytes(b"wrong old build")
        self.depends = {"consumer": "provider,builtin", "provider": ""}
        self.version = "6.12.23-test SMP preempt mod_unload aarch64"

    def info(self, *args):
        field, name = args[2], Path(args[3]).stem
        if field == "vermagic":
            return self.version
        if field == "depends":
            return self.depends[name]
        if field == "softdep":
            return ""
        raise AssertionError(args)

    def run_stage(self, extra=None):
        with patch.object(sys, "argv", ["stage_modules.py", str(self.kernel),
                          str(self.external), str(self.dest)] + (extra or [])), \
             patch.object(stage_modules, "output", self.info), \
             patch.object(stage_modules.subprocess, "run"), \
             contextlib.redirect_stdout(io.StringIO()):
            stage_modules.main()

    def test_order_and_exclude_stale(self):
        self.run_stage()
        self.assertEqual((self.dest / "modules.load").read_text(), "provider.ko\nconsumer.ko\n")
        manifest = json.loads((self.dest / "manifest.json").read_text())
        self.assertEqual(set(manifest["modules"]), {"provider", "consumer"})
        self.assertFalse((self.dest / "stale.ko").exists())

    def test_missing_dependency(self):
        self.depends["consumer"] = "missing"
        with self.assertRaisesRegex(ValueError, "Missing module dependency"):
            self.run_stage()
        self.assertFalse(self.dest.exists())

    def test_explicit_common_root_without_symbol_dependency(self):
        self.depends["consumer"] = ""
        self.run_stage(["--root", "provider"])
        self.assertEqual((self.dest / "modules.load").read_text(), "consumer.ko\nprovider.ko\n")

    def test_missing_explicit_root(self):
        with self.assertRaisesRegex(ValueError, "Missing module dependency: absent"):
            self.run_stage(["--root", "absent"])
        self.assertFalse(self.dest.exists())

    def test_dependency_cycle(self):
        self.depends["provider"] = "consumer"
        with self.assertRaisesRegex(ValueError, "cycle"):
            self.run_stage()

    def test_release_mismatch(self):
        self.version = "4.14.336 SMP aarch64"
        with self.assertRaisesRegex(ValueError, "Wrong kernel release"):
            self.run_stage()

    def add_power_modules(self):
        names = ["cannon-scpsys-core", "mtk-scpsys-mt6853", "consumer", "ufs-mt6853"]
        (self.external / "modules.order").write_text("".join(n + ".o\n" for n in names))
        for name in names:
            (self.external / (name + ".ko")).write_bytes(name.encode())
        self.depends.update({"cannon-scpsys-core": "provider",
                             "mtk-scpsys-mt6853": "cannon-scpsys-core",
                             "ufs-mt6853": "consumer"})

    def test_power_probes_follow_ufs_and_its_functional_providers(self):
        self.add_power_modules()
        self.run_stage()
        self.assertEqual((self.dest / "modules.load").read_text().splitlines(),
                         ["provider.ko", "consumer.ko", "ufs-mt6853.ko",
                          "cannon-scpsys-core.ko", "mtk-scpsys-mt6853.ko"])

    def test_reject_dependency_that_powers_on_before_recovery_is_armed(self):
        self.add_power_modules()
        self.depends["consumer"] = "cannon-scpsys-core"
        with self.assertRaisesRegex(ValueError, "before the UFS Recovery barrier"):
            self.run_stage()
        self.assertFalse(self.dest.exists())


if __name__ == "__main__":
    unittest.main()
