#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Create a guarded source release archive without build/cache output."""
from __future__ import annotations

import os
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_NAME = "CarService_release_checked.zip"
SOURCE_DIRS = {"src", "cmake", "scripts", "resources", "tests"}
SOURCE_FILES = {"CMakeLists.txt", "CarService_PointG_v2_1.iss",
                "github_release_manifest.json", "GITHUB_RELEASE_UPLOAD_LIST_2.1.txt"}
EXCLUDED_DIRS = {".git", ".vs", ".codex", ".agents", "private-data", "tmp",
                 "logs", "cache", "backups", "__pycache__", ".venv", "venv"}
EXCLUDED_SUFFIXES = {".pyc", ".pdb", ".ilk", ".obj", ".o", ".tmp", ".part",
                     ".db", ".sqlite", ".sqlite3", ".pfx", ".p12", ".pem", ".key"}


def should_include(path: Path) -> bool:
    rel = path.relative_to(ROOT)
    parts = tuple(part.lower() for part in rel.parts)
    if not parts or (parts[0] not in SOURCE_DIRS and rel.as_posix() not in SOURCE_FILES):
        return False
    if any(part in EXCLUDED_DIRS or part in {"build", "dist"}
           or part.startswith(("build-", "build_", "dist-", "dist_", ".env"))
           for part in parts):
        return False
    if path.suffix.lower() in EXCLUDED_SUFFIXES or path.name.lower().endswith(("-wal", "-shm", "-journal")):
        return False
    if is_link(path) or any(is_link(parent)
                                for parent in path.parents if parent != ROOT.parent):
        return False
    return True


def is_link(path: Path) -> bool:
    return path.is_symlink() or bool(getattr(path.lstat(), "st_file_attributes", 0) & 0x400)


def main(argv: list[str]) -> int:
    out_name = argv[0] if argv else DEFAULT_NAME
    out_path = ROOT / out_name
    if out_path.parent.resolve() != ROOT.resolve() or out_path.suffix.lower() != ".zip":
        raise ValueError("Archive output must be a ZIP directly inside the project")
    if out_path.exists():
        raise FileExistsError("Archive already exists; use a fresh output name")
    created = False
    try:
        with out_path.open("xb") as output:
            created = True
            with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
                for directory, dirs, files in os.walk(ROOT, followlinks=False):
                    base = Path(directory)
                    dirs[:] = sorted(d for d in dirs if should_include(base / d))
                    for name in sorted(files):
                        path = base / name
                        if should_include(path):
                            zf.write(path, path.relative_to(ROOT).as_posix())
    except Exception:
        if created:
            out_path.unlink(missing_ok=True)
        raise
    print(f"release archive created: {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
