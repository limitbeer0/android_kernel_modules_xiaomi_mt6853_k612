#!/usr/bin/env python3
"""Keep a boot-v2 base's ramdisk/header, use a compatible donor kernel + DTB."""
import argparse
import hashlib
from pathlib import Path
import struct

from inspect_stock import boot_parts


def repack(base, donor):
    info, stock = boot_parts(base)
    other, source = boot_parts(donor)
    if info["header_version"] != 2 or other["header_version"] != 2:
        raise ValueError("both images must be boot v2")
    for key in ("page_size", "kernel_addr", "ramdisk_addr", "tags_addr", "dtb_addr"):
        if info[key] != other[key]:
            raise ValueError(f"incompatible donor {key}")
    if info["second_size"] or other["second_size"]:
        raise ValueError("second-stage payload is not supported")
    page = info["page_size"]
    align = lambda size: (size + page - 1) // page * page
    kernel, ramdisk = source["kernel"], stock["ramdisk"]
    recovery, dtb = stock.get("recovery_dtbo", b""), source["dtb"]
    header = bytearray(base[:page])
    struct.pack_into("<I", header, 8, len(kernel))
    struct.pack_into("<I", header, 1648, len(dtb))
    if recovery:
        struct.pack_into("<Q", header, 1636, page + align(len(kernel)) + align(len(ramdisk)))
    digest = hashlib.sha1()
    for data in (kernel, ramdisk, b"", recovery, dtb):
        digest.update(data)
        digest.update(struct.pack("<I", len(data)))
    header[576:608] = digest.digest() + bytes(12)
    result = bytearray(header)
    for data in (kernel, ramdisk, recovery, dtb):
        result.extend(data)
        result.extend(bytes(-len(result) % page))
    if len(result) > 0x4000000:
        raise ValueError("cannon boot exceeds 64 MiB")
    check, parts = boot_parts(result)
    assert parts["ramdisk"] == ramdisk
    assert parts["kernel"] == kernel and parts["dtb"] == dtb
    assert check["cmdline"] == info["cmdline"]
    return bytes(result)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("base", type=Path)
    p.add_argument("donor", type=Path)
    p.add_argument("output", type=Path)
    args = p.parse_args()
    image = repack(args.base.read_bytes(), args.donor.read_bytes())
    with args.output.open("xb") as f:
        f.write(image)
    print(len(image), hashlib.sha256(image).hexdigest(), args.output)


if __name__ == "__main__":
    main()
