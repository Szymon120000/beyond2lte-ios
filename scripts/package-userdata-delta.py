#!/usr/bin/env python3
"""Build a file-level TWRP delta ZIP between two prepared HFS root trees.

Only changed/new regular files are included. Removed files are never deleted.
Symlink target changes are rejected rather than silently omitted.
"""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import stat
import zipfile

UPDATER_PATH = "META-INF/com/google/android/update-binary"
MANIFEST_PATH = "META-INF/com/google/android/userdata-manifest.tsv"


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def inventory(root: Path):
    result = {}
    for base, dirs, files in os.walk(root, followlinks=False):
        base_path = Path(base)
        for name in list(dirs):
            path = base_path / name
            if path.is_symlink():
                rel = path.relative_to(root).as_posix()
                result[rel] = ("symlink", os.readlink(path))
                dirs.remove(name)
        for name in files:
            path = base_path / name
            rel = path.relative_to(root).as_posix()
            if path.is_symlink():
                result[rel] = ("symlink", os.readlink(path))
            elif path.is_file():
                result[rel] = ("file", digest(path), stat.S_IMODE(path.stat().st_mode), path)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base_root", type=Path)
    parser.add_argument("new_root", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    base = args.base_root.resolve()
    new = args.new_root.resolve()
    if not base.is_dir() or not new.is_dir() or base == new:
        raise SystemExit("base_root and new_root must be distinct existing directories")

    old = inventory(base)
    fresh = inventory(new)
    changed = []
    for rel, item in fresh.items():
        if item[0] == "symlink":
            if old.get(rel) != item:
                raise SystemExit(f"changed/new symlink is not supported by TWRP delta updater: {rel}")
            continue
        previous = old.get(rel)
        if previous is None or previous[0] != "file" or previous[1] != item[1] or previous[2] != item[2]:
            if any(part in ("", ".", "..") for part in Path(rel).parts) or "\t" in rel or "\n" in rel or "\r" in rel:
                raise SystemExit(f"unsafe path cannot be represented in manifest: {rel}")
            changed.append((rel, item))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        archive.writestr(UPDATER_PATH, UPDATER, compress_type=zipfile.ZIP_STORED)
        lines = []
        for rel, item in sorted(changed):
            _, sha, mode, source = item
            archive.write(source, f"payload/{rel}")
            lines.append(f"{sha}\t{mode:04o}\t{rel}\n")
        archive.writestr(MANIFEST_PATH, "".join(lines))
    print(f"Created {args.output}: {len(changed)} changed/new files")
    print("No removed files are deleted; changed symlinks are rejected.")
    print("TWRP must support read-write HFS+ on the target USERDATA volume; otherwise updater exits without changes.")


if __name__ == "__main__":
    main()
