#!/usr/bin/env python3
"""Extract only cannon's boot metadata; never unpack the large super image."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import struct
import tarfile


def boot_parts(data):
    if data[:8] != b"ANDROID!":
        raise ValueError("not an Android boot image")
    fields = struct.unpack_from("<10I", data, 8)
    ks, ka, rs, ra, ss, sa, ta, page, version, osver = fields
    if version not in (0, 1, 2) or page not in (2048, 4096, 8192, 16384):
        raise ValueError(f"unsupported boot layout: header={version}, page={page}")
    align = lambda n: (n + page - 1) // page * page
    offsets = {"kernel": (page, ks), "ramdisk": (page + align(ks), rs)}
    end = page + align(ks) + align(rs) + align(ss)
    info = dict(header_version=version, page_size=page, kernel_addr=hex(ka),
                ramdisk_addr=hex(ra), tags_addr=hex(ta), os_version=hex(osver),
                kernel_size=ks, ramdisk_size=rs, second_size=ss,
                cmdline=(data[64:576] + data[608:1632]).split(b"\0")[0].decode())
    if version >= 1:
        # Explicit little endian avoids native alignment at recovery_dtbo_offset.
        size = struct.unpack_from("<I", data, 1632)[0]
        offset = struct.unpack_from("<Q", data, 1636)[0]
        if size:
            offsets["recovery_dtbo"] = (offset, size)
        end += align(size)
    if version == 2:
        size, addr = struct.unpack_from("<IQ", data, 1648)
        info.update(dtb_size=size, dtb_addr=hex(addr))
        offsets["dtb"] = (end, size)
    parts = {name: data[off:off+size] for name, (off, size) in offsets.items()}
    for name, (off, size) in offsets.items():
        if off + size > len(data):
            raise ValueError(f"truncated {name}")
    info["sha256"] = hashlib.sha256(data).hexdigest()
    return info, parts


def cpio_entries(blob):
    pos = 0
    entries = {}
    while pos + 110 <= len(blob):
        if blob[pos:pos+6] not in (b"070701", b"070702"):
            raise ValueError(f"invalid newc header at {pos}")
        fields = [int(blob[pos+6+i*8:pos+14+i*8], 16) for i in range(13)]
        mode, size, namesize = fields[1], fields[6], fields[11]
        name = blob[pos+110:pos+110+namesize-1].decode()
        start = (pos + 110 + namesize + 3) & ~3
        if name == "TRAILER!!!":
            break
        entries[name] = (mode, blob[start:start+size])
        pos = (start + size + 3) & ~3
    return entries


def main():
    p = argparse.ArgumentParser()
    p.add_argument("archive", type=Path)
    p.add_argument("out", type=Path)
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    wanted = {"boot.img", "dtbo.img", "MT6853_Android_scatter.txt"}
    with tarfile.open(args.archive) as archive:
        for entry in archive:
            name = Path(entry.name).name
            if entry.isfile() and name in wanted:
                (args.out / name).write_bytes(archive.extractfile(entry).read())
                wanted.remove(name)
                if not wanted:
                    break
    if wanted:
        raise ValueError(f"missing stock files: {wanted}")
    info, parts = boot_parts((args.out / "boot.img").read_bytes())
    for name, content in parts.items():
        (args.out / name).write_bytes(content)
    dtb = parts.get("dtb", b"")
    if dtb[:4] == bytes.fromhex("d7b7ab1e"):
        _, total, header, entry_size, count, entries_off, _, _ = struct.unpack_from(">8I", dtb)
        if count != 1 or total > len(dtb) or header < 32 or entry_size < 32:
            raise ValueError("expected one stock DTB in MTK DT table")
        size, offset = struct.unpack_from(">2I", dtb, entries_off)
        if offset + size > len(dtb):
            raise ValueError("truncated DT table entry")
        (args.out / "base.dtb").write_bytes(dtb[offset:offset+size])
    ramdisk = parts["ramdisk"]
    info["ramdisk_compression"] = "gzip" if ramdisk[:2] == b"\x1f\x8b" else "unknown"
    if info["ramdisk_compression"] == "gzip":
        ramdisk = gzip.decompress(ramdisk)
        (args.out / "ramdisk.cpio").write_bytes(ramdisk)
        entries = cpio_entries(ramdisk)
        (args.out / "ramdisk-files.txt").write_text("\n".join(sorted(entries)) + "\n")
        for name, (_, content) in entries.items():
            if name.endswith((".rc", ".fstab")) or "fstab" in name:
                dest = args.out / "ramdisk-text" / name.lstrip("/")
                if ".." not in dest.parts:
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    dest.write_bytes(content)
        info["ramdisk_entries"] = len(entries)
        info["init"] = {"mode": oct(entries["init"][0]), "size": len(entries["init"][1]),
                        "magic": entries["init"][1][:4].hex()}
    (args.out / "boot.json").write_text(json.dumps(info, indent=2) + "\n")
    print(json.dumps(info, indent=2))


if __name__ == "__main__":
    main()
