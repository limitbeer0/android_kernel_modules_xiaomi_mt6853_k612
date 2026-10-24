#!/usr/bin/env python3
"""Build an unsigned cannon boot-v2 image with a PID1 module-loader ramdisk.

Preserves stock addresses, header, DT table identity and ramdisk ownership/modes.
Requires a freshly staged, dependency-checked module set and a compiled cannon DTB.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import struct
import zlib

from inspect_stock import boot_parts


def read_cpio(blob):
    pos, entries = 0, {}
    while pos + 110 <= len(blob):
        if blob[pos:pos+6] != b"070701":
            raise ValueError(f"unsupported cpio at {pos}")
        f = [int(blob[pos+6+i*8:pos+14+i*8], 16) for i in range(13)]
        name = blob[pos+110:pos+109+f[11]].decode()
        start = (pos+110+f[11]+3) & ~3
        if start + f[6] > len(blob):
            raise ValueError("truncated cpio")
        if name == "TRAILER!!!":
            return entries
        if name.startswith("/") or ".." in Path(name).parts or name in entries:
            raise ValueError(f"unsafe/duplicate cpio path: {name}")
        entries[name] = (f, blob[start:start+f[6]])
        pos = (start+f[6]+3) & ~3
    raise ValueError("cpio trailer missing")


def write_cpio(entries):
    out = bytearray()
    for ino, (name, (original, data)) in enumerate(entries.items(), 1):
        f = original.copy()
        encoded = name.encode() + b"\0"
        f[0], f[6], f[11], f[12] = ino, len(data), len(encoded), 0
        out.extend(b"070701" + "".join(f"{n:08x}" for n in f).encode() + encoded)
        out.extend(b"\0" * (-len(out) % 4))
        out.extend(data)
        out.extend(b"\0" * (-len(out) % 4))
    return bytes(out)


def entry(mode, data=b""):
    return ([0, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, 0, 0], data)


def unpack_ramdisk(blob):
    if blob[:2] == b"\x1f\x8b":
        return "gzip", gzip.decompress(blob)
    if blob[:6] == b"070701":
        return "none", blob
    raise ValueError("unsupported ramdisk compression")


def guard_test_fstab(data):
    """A failed bring-up mount must not request formatting, repair or resizing."""
    result = []
    for line in data.decode().splitlines(keepends=True):
        fields = line.split()
        if not fields or fields[0].startswith("#") or len(fields) != 5:
            result.append(line)
            continue
        flags = fields[4].split(",")
        guarded = [flag for flag in flags if flag not in {"formattable", "check", "resize"}]
        if flags == guarded:
            result.append(line)
        else:
            fields[4] = ",".join(guarded) or "defaults"
            result.append("\t".join(fields) + ("\n" if line.endswith("\n") else ""))
    return "".join(result).encode()


def pack(stock, kernel, dtb, loader, modules, watchdog_test=False, watchdog_android=False,
         crypto_probe=None, crypto_write=False, watchdog_android_long=False, native_expdb=False,
         adb_root=False):
    if crypto_write and crypto_probe is None:
        raise ValueError("write test needs a crypto probe")
    if sum((bool(watchdog_test), bool(watchdog_android), bool(watchdog_android_long), crypto_probe is not None)) > 1:
        raise ValueError("diagnostic modes are mutually exclusive")
    if native_expdb and not watchdog_android:
        raise ValueError("native expdb capture requires the Android watchdog diagnostic")
    if adb_root and not native_expdb:
        raise ValueError("adb root requires the bounded native expdb diagnostic")
    info, parts = boot_parts(stock)
    if info["header_version"] != 2 or info["second_size"]:
        raise ValueError("expected stock cannon v2 boot without second stage")
    compression, original_cpio = unpack_ramdisk(parts["ramdisk"])
    if kernel[:2] != b"\x1f\x8b":
        raise ValueError("expected Image.gz")
    # Verify decompression, kernel arm64 Image magic, and loader ELF machine.
    image = gzip.decompress(kernel)
    if adb_root and b"cannon-adbroot: authenticated" not in image:
        raise ValueError("kernel lacks the cannon adbroot bootstrap")
    if image[56:60] != b"ARM\x64":
        raise ValueError("kernel is not an ARM64 Image.gz")
    if loader[:5] != b"\x7fELF\x02" or struct.unpack_from("<H", loader, 18)[0] != 183:
        raise ValueError("loader must be ELF64 AArch64")
    if dtb[:4] != bytes.fromhex("d00dfeed"):
        raise ValueError("invalid compiled cannon DTB")
    # The running cannon boot uses Image.gz followed by a bare FDT in the
    # kernel payload, in addition to the v2 DT table slot. Preserve that
    # loader contract when replacing its kernel.
    decoder = zlib.decompressobj(31)
    decoder.decompress(parts["kernel"])
    tail = decoder.unused_data
    if tail:
        if tail[:4] != bytes.fromhex("d00dfeed") or len(tail) < 8:
            raise ValueError("unsupported trailing data in base kernel payload")
        if struct.unpack_from(">I", tail, 4)[0] != len(tail):
            raise ValueError("invalid appended DTB size in base kernel payload")
        kernel += dtb
    entries = read_cpio(original_cpio)
    if "cannon-init" in entries or "init.stock" in entries or "lib/modules/modules.load" in entries:
        raise ValueError("stock ramdisk was already modified")
    if entries["init"][1][:4] != b"\x7fELF":
        raise ValueError("stock /init must be an executable file")
    # The running ramdisk contains formattable metadata/data. Preserve its
    # layout and permissions, but never auto-format/repair/resize during tests.
    for name, (fields, data) in list(entries.items()):
        if Path(name).name.startswith("fstab.") and data:
            entries[name] = (fields, guard_test_fstab(data))
    entries["cannon-init"] = entry(0o100750, loader)
    if crypto_probe is not None:
        if (len(crypto_probe) < 64 or crypto_probe[:5] != b"\x7fELF\x02" or
                struct.unpack_from("<H", crypto_probe, 18)[0] != 183):
            raise ValueError("crypto probe must be ELF64 AArch64")
        phoff = struct.unpack_from("<Q", crypto_probe, 32)[0]
        phsize, phnum = struct.unpack_from("<HH", crypto_probe, 54)
        if phnum and (phsize < 56 or phoff + phsize * phnum > len(crypto_probe)):
            raise ValueError("invalid crypto probe ELF program headers")
        if any(struct.unpack_from("<I", crypto_probe, phoff + i * phsize)[0] == 3
               for i in range(phnum)):
            raise ValueError("crypto probe must be static (no ELF interpreter)")
        entries["cannon-fscrypt-probe"] = entry(0o100750, crypto_probe)
        entries["cannon-watchdog-test"] = entry(0o100600, b"G" if crypto_write else b"F")
    elif watchdog_test or watchdog_android or watchdog_android_long:
        entries["cannon-watchdog-test"] = entry(0o100600, b"L" if watchdog_android_long else
                                               b"A" if watchdog_android else b"")
    for name in ("lib", "lib/modules"):
        entries.setdefault(name, entry(0o40755))
    manifest = json.loads((modules / "manifest.json").read_text())
    if ("Linux version " + manifest["kernel_release"] + " ").encode() not in image:
        raise ValueError("Image.gz release differs from the staged module set")
    names = (modules / "modules.load").read_text().splitlines()
    if not names or len(names) != len(set(names)):
        raise ValueError("empty or duplicate modules.load")
    for name in names:
        if Path(name).name != name or not name.endswith(".ko"):
            raise ValueError(f"invalid module path: {name}")
        data = (modules / name).read_bytes()
        record = manifest["modules"][name[:-3].replace("-", "_")]
        if hashlib.sha256(data).hexdigest() != record["sha256"]:
            raise ValueError(f"staged module changed: {name}")
        entries["lib/modules/" + name] = entry(0o100644, data)
    entries["lib/modules/modules.load"] = entry(0o100644, (modules / "modules.load").read_bytes())
    entries["TRAILER!!!"] = entry(0)
    raw_cpio = write_cpio(entries)
    ramdisk = gzip.compress(raw_cpio, mtime=0) if compression == "gzip" else raw_cpio
    # MTK expects a DT table in boot's dtb slot, not a bare FDT.
    old = parts["dtb"]
    fields = struct.unpack_from(">8I", old)
    if fields[0] != 0xd7b7ab1e or fields[4] != 1:
        raise ValueError("unsupported stock MTK DT table")
    eo = fields[5]
    _, off = struct.unpack_from(">II", old, eo)
    table = bytearray(old[:off])
    struct.pack_into(">I", table, 4, off + len(dtb))
    struct.pack_into(">I", table, eo, len(dtb))
    table.extend(dtb)
    page = info["page_size"]
    header = bytearray(stock[:page])
    cmdline = info["cmdline"].split()
    if any(arg.startswith("rdinit=") for arg in cmdline):
        raise ValueError("base boot already has rdinit; resolve its init contract first")
    cmdline.append("rdinit=/cannon-init")
    if native_expdb:
        if any(arg.startswith(("cannon.log_store=", "cannon.expdb=", "cannon.expdb_seconds=", "mtk_wdt.cannon_budget=")) for arg in cmdline):
            raise ValueError("base boot already selects native log diagnostics")
        cmdline.extend(("cannon.log_store=1", "cannon.expdb=1", "cannon.expdb_seconds=660", "mtk_wdt.cannon_budget=600"))
    if adb_root:
        if any(arg.startswith("cannon.adbroot=") for arg in cmdline):
            raise ValueError("base boot already selects adb root")
        cmdline.append("cannon.adbroot=1")
    encoded = " ".join(cmdline).encode()
    if len(encoded) >= 1536:
        raise ValueError("boot command line exceeds v2 header capacity")
    header[64:576] = encoded[:512].ljust(512, b"\0")
    header[608:1632] = encoded[512:].ljust(1024, b"\0")
    struct.pack_into("<I", header, 8, len(kernel))
    struct.pack_into("<I", header, 16, len(ramdisk))
    struct.pack_into("<I", header, 1648, len(table))
    recovery = parts.get("recovery_dtbo", b"")
    align = lambda n: (n + page - 1) // page * page
    if recovery:
        struct.pack_into("<Q", header, 1636, page+align(len(kernel))+align(len(ramdisk)))
    digest = hashlib.sha1()
    for content in (kernel, ramdisk, b"", recovery, table):
        digest.update(content)
        digest.update(struct.pack("<I", len(content)))
    header[576:608] = digest.digest() + bytes(12)
    out = bytearray(header)
    for content in (kernel, ramdisk, recovery, table):
        out.extend(content)
        out.extend(bytes(-len(out) % page))
    if len(out) > 0x4000000:
        raise ValueError("boot image exceeds cannon 64MiB boot partition")
    # No stock AVB footer/signature is reused after changing the payload.
    check, new = boot_parts(out)
    assert new["kernel"] == kernel and new["dtb"] == table
    _, check_cpio = unpack_ramdisk(new["ramdisk"])
    assert "cannon-init" in read_cpio(check_cpio)
    return out


def main():
    p = argparse.ArgumentParser()
    for name in ("stock", "kernel", "dtb", "loader", "modules", "output"):
        p.add_argument("--"+name, type=Path, required=True)
    p.add_argument("--watchdog-test", action="store_true")
    p.add_argument("--watchdog-android-test", action="store_true")
    p.add_argument("--watchdog-android-long-test", action="store_true")
    p.add_argument("--native-expdb", action="store_true",
                   help="enable native 512KiB + direct 8MiB expdb capture and a bounded 600s WDT window")
    p.add_argument("--adb-root", action="store_true", help="enable the diagnostic adbd preload/credentials bootstrap")
    p.add_argument("--crypto-probe", type=Path,
                   help="static ARM64 reader; selects read-only fscrypt diagnostic mode")
    p.add_argument("--crypto-write", action="store_true",
                   help="with --crypto-probe: create two synthetic fixtures after read checks pass")
    args = p.parse_args()
    if args.output.exists():
        raise SystemExit("Refusing to overwrite an existing boot artifact")
    image = pack(args.stock.read_bytes(), args.kernel.read_bytes(), args.dtb.read_bytes(),
                 args.loader.read_bytes(), args.modules, watchdog_test=args.watchdog_test,
                 watchdog_android=args.watchdog_android_test,
                 crypto_probe=args.crypto_probe.read_bytes() if args.crypto_probe else None,
                 crypto_write=args.crypto_write,
                 watchdog_android_long=args.watchdog_android_long_test,
                 native_expdb=args.native_expdb, adb_root=args.adb_root)
    args.output.write_bytes(image)
    manifest = json.loads((args.modules / "manifest.json").read_text())
    metadata = dict(status="development-image-not-device-validated", size=len(image),
                    native_expdb_requested=args.native_expdb,
                    adb_root_requested=args.adb_root,
                    bounded_kernel_keepalives=28 if args.native_expdb else 0,
                    kernel_watchdog_budget_seconds=600 if args.native_expdb else 0,
                    direct_spool_lifetime_seconds=660 if args.native_expdb else 0,
                    fstab_auto_format_repair_resize=False,
                    diagnostic_watchdog=bool(args.watchdog_test or args.watchdog_android_test or args.watchdog_android_long_test or args.crypto_probe),
                    diagnostic_mode="fscrypt-synthetic-write" if args.crypto_write else
                                    "fscrypt-read-only" if args.crypto_probe else
                                    "android-init-90s" if args.watchdog_android_long_test else
                                    "android-init" if args.watchdog_android_test else
                                    ("probe-only" if args.watchdog_test else "none"),
                    sha256=hashlib.sha256(image).hexdigest(),
                    stock_sha256=hashlib.sha256(args.stock.read_bytes()).hexdigest(),
                    kernel_sha256=hashlib.sha256(args.kernel.read_bytes()).hexdigest(),
                    dtb_sha256=hashlib.sha256(args.dtb.read_bytes()).hexdigest(),
                    loader_sha256=hashlib.sha256(args.loader.read_bytes()).hexdigest(),
                    module_count=len(manifest["modules"]), module_manifest=manifest)
    args.output.with_suffix(args.output.suffix + ".json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"Unsigned development boot image: {args.output} ({len(image)} bytes)")


if __name__ == "__main__":
    main()
