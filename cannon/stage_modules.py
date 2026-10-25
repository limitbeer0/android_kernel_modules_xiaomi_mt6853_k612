#!/usr/bin/env python3
"""Stage same-build modules in dependency order, including in-tree providers."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def output(*args):
    return subprocess.check_output(args, text=True).strip()


def key(name):
    return name.removesuffix(".ko").replace("-", "_")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("kernel_out", type=Path)
    parser.add_argument("module_tree", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--strip", default="llvm-strip-13")
    parser.add_argument("--root", action="append", default=[],
                        help="additional current-build module to load (for common device drivers)")
    args = parser.parse_args()
    release = (args.kernel_out / "include/config/kernel.release").read_text().strip()
    # A modules_prepare-only tree has no audited kernel export set.
    for name in ("Module.symvers", "System.map"):
        if not (args.kernel_out / name).is_file():
            raise SystemExit(f"Build kernel first: missing {name}")
    if args.destination.exists() and any(args.destination.iterdir()):
        raise SystemExit("Destination must be empty (prevents stale module packaging)")
    candidates = {}
    roots = []
    for tree, external in ((args.kernel_out, False), (args.module_tree, True)):
        # modules.order contains only the current configuration's modules.
        # Walking *.ko would also package stale outputs from a previous target.
        order = tree / "modules.order"
        if not order.is_file():
            raise SystemExit(f"Missing current build module list: {order}")
        for line in order.read_text().splitlines():
            path = Path(line)
            # Linux 6.12 modules.order lists .o link inputs; older kernels
            # listed .ko outputs. Both describe the same final module paths.
            if path.suffix == ".o":
                path = path.with_suffix(".ko")
            if not path.is_absolute():
                path = tree / path
            if not path.is_file() or path.suffix != ".ko":
                raise SystemExit(f"Unbuilt module listed in {order}: {path}")
            name = key(path.name)
            if name in candidates and candidates[name] != path:
                raise SystemExit(f"Duplicate module name: {name}")
            candidates[name] = path
            if external:
                roots.append(name)
    if not roots:
        raise SystemExit("No cannon device modules were built")
    roots.extend(args.root)
    # A SCPSYS probe can touch power/reset state. Run it only after the UFS
    # module, when cannon-init has synced and verified the Recovery BCB.
    # Keep the existing functional ordering of clocks, PMIC and UFS intact.
    power_roots = {"cannon_scpsys_core", "mtk_scpsys_mt6853"}
    delayed = [name for name in roots if key(name) in power_roots]
    if delayed:
        roots = [name for name in roots if key(name) not in power_roots]
        if "ufs_mt6853" not in [key(name) for name in roots]:
            raise ValueError("SCPSYS requires the UFS Recovery barrier")
        index = [key(name) for name in roots].index("ufs_mt6853") + 1
        roots[index:index] = delayed
    builtins = {key(Path(line).name) for line in
                (args.kernel_out / "modules.builtin").read_text().splitlines()}
    done, visiting, ordered, records = set(), set(), [], {}
    vermagic = None

    def visit(name):
        nonlocal vermagic
        name = key(name)
        if name in done or name in builtins:
            return
        if name in visiting:
            raise ValueError(f"Module dependency cycle at {name}")
        path = candidates.get(name)
        if path is None:
            raise ValueError(f"Missing module dependency: {name}")
        visiting.add(name)
        version = output("modinfo", "-F", "vermagic", str(path))
        if version.split()[0] != release:
            raise ValueError(f"Wrong kernel release: {path}: {version}")
        if vermagic is None:
            vermagic = version
        elif vermagic != version:
            raise ValueError(f"Inconsistent vermagic: {path}: {version}")
        depends = [x for x in output("modinfo", "-F", "depends", str(path)).split(",") if x]
        soft = output("modinfo", "-F", "softdep", str(path))
        # Early bring-up must not silently discard an explicit ordering contract.
        if soft:
            raise ValueError(f"Add explicit softdep support before staging {path}: {soft}")
        for dependency in depends:
            visit(dependency)
        visiting.remove(name)
        done.add(name)
        ordered.append(path)
        records[name] = dict(source=str(path.resolve()), depends=depends)

    for name in roots:
        visit(name)
    if delayed:
        names = [key(path.name) for path in ordered]
        if any(names.index(key(name)) < names.index("ufs_mt6853") for name in delayed):
            raise ValueError("SCPSYS dependency loads before the UFS Recovery barrier")
    args.destination.mkdir(parents=True, exist_ok=True)
    for path in ordered:
        dest = args.destination / path.name
        shutil.copyfile(path, dest)
        subprocess.run([args.strip, "--strip-debug", str(dest)], check=True)
        records[key(path.name)]["sha256"] = hashlib.sha256(dest.read_bytes()).hexdigest()
    (args.destination / "modules.load").write_text("".join(p.name + "\n" for p in ordered))
    (args.destination / "manifest.json").write_text(json.dumps(dict(
        kernel_release=release, vermagic=vermagic, modules=records), indent=2) + "\n")
    print(f"Staged {len(ordered)} modules for {release}; dependency order validated")


if __name__ == "__main__":
    main()
