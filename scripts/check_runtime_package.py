#!/usr/bin/env python3
"""Offline runtime inventory gate. Hashes prove inventory consistency, not trust."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
from pathlib import Path

MANIFEST = "runtime-manifest.json"
RUNTIME_SCRIPTS = {"install_ocr_runtime.ps1", "check_ocr_runtime.ps1",
                   "prepare_ocr_runtime_from_installers.ps1"}
PLUGIN_DIRS = {"generic", "iconengines", "imageformats", "networkinformation",
               "platforms", "sqldrivers", "styles", "tls"}
REQUIRED = {"CarServicePointG.exe", "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll",
            "Qt6Network.dll", "Qt6Sql.dll", "platforms/qwindows.dll",
            "sqldrivers/qsqlite.dll", "resources/windows/ocr-layer/poppler/bin/pdfinfo.exe",
            "resources/windows/ocr-layer/poppler/bin/pdftoppm.exe",
            "resources/windows/ocr-layer/poppler/bin/pdftotext.exe"}
REQUIRED |= {"scripts/" + name for name in RUNTIME_SCRIPTS}
REQUIRED |= {"resources/assets/" + name for name in
             ("app_icon.ico", "app_icon.png", "app_shortcut.ico", "app_shortcut.png")}
PRIVATE_DIRS = {"private-data", "logs", "cache", "backups", ".git", ".codex", ".aws"}
PRIVATE_SUFFIXES = {".db", ".sqlite", ".sqlite3", ".pfx", ".p12", ".pem", ".key", ".log"}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def pe_machine(path: Path) -> int:
    with path.open("rb") as stream:
        header = stream.read(64)
        if len(header) != 64 or header[:2] != b"MZ":
            raise ValueError("Invalid PE header")
        stream.seek(struct.unpack_from("<I", header, 60)[0])
        pe = stream.read(6)
        if len(pe) != 6 or pe[:4] != b"PE\0\0":
            raise ValueError("Invalid PE signature")
        return struct.unpack_from("<H", pe, 4)[0]


def allowed(rel: Path) -> bool:
    parts = tuple(part.lower() for part in rel.parts)
    if any(part in PRIVATE_DIRS or part.startswith(".env") for part in parts):
        return False
    if rel.suffix.lower() in PRIVATE_SUFFIXES or parts[-1].endswith(("-wal", "-shm", "-journal")):
        return False
    if len(parts) == 1:
        return rel.name in {"CarServicePointG.exe", "vc_redist.x64.exe", "qt.conf", MANIFEST} or rel.suffix.lower() == ".dll"
    if parts[0] in PLUGIN_DIRS:
        return rel.suffix.lower() == ".dll"
    if parts[0] == "translations":
        return rel.suffix.lower() == ".qm"
    if parts[0] == "scripts":
        return len(parts) == 2 and rel.name in RUNTIME_SCRIPTS
    return parts[:2] in {("resources", "assets"), ("resources", "python_vendor")} or parts[:3] == ("resources", "windows", "ocr-layer")


def inventory(root: Path) -> dict:
    entries = {}
    for directory, dirs, files in os.walk(root, followlinks=False):
        base = Path(directory)
        for name in dirs + files:
            path = base / name
            if path.is_symlink() or getattr(path.lstat(), "st_file_attributes", 0) & 0x400:
                raise ValueError("Runtime contains a symbolic link or junction")
        for name in sorted(files):
            path = base / name
            rel = path.relative_to(root)
            if not allowed(rel):
                raise ValueError(f"Non-runtime or private file rejected: {rel.as_posix()}")
            if rel.as_posix() == MANIFEST:
                continue
            # Qt and the app must all target x64. Third-party helpers may differ.
            if ((len(rel.parts) == 1 and (name == "CarServicePointG.exe" or name.startswith("Qt6")))
                    or rel.parts[0] in PLUGIN_DIRS):
                if pe_machine(path) != 0x8664:
                    raise ValueError(f"Non-x64 runtime module: {rel.as_posix()}")
            entries[rel.as_posix()] = {"size": path.stat().st_size, "sha256": sha256(path)}
    missing = REQUIRED - entries.keys()
    if missing:
        raise ValueError("Required runtime files missing: " + ", ".join(sorted(missing)))
    return entries


def check(root: Path, write: bool = False) -> int:
    entries = inventory(root)
    manifest_path = root / MANIFEST
    if write:
        with manifest_path.open("x", encoding="utf-8") as stream:
            json.dump({"schema": 1, "architecture": "x64", "files": entries}, stream, indent=2, sort_keys=True)
            stream.write("\n")
    else:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest.get("schema") != 1 or manifest.get("architecture") != "x64" or manifest.get("files") != entries:
            raise ValueError("Runtime inventory/hash mismatch")
    print(f"RUNTIME_PACKAGE_GATE OK: {len(entries)} files; x64 Qt/app; no launch or network")
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--write-manifest", action="store_true")
    args = parser.parse_args()
    try:
        raise SystemExit(check(args.runtime.resolve(strict=True), args.write_manifest))
    except (OSError, ValueError) as error:
        parser.exit(1, f"RUNTIME_PACKAGE_GATE FAILED: {error}\n")
