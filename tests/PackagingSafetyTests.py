"""Offline packaging regressions; all writable fixtures live in TemporaryDirectory."""
import importlib.util
import struct
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / f"{name}.py")
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


archive = module("create_release_archive")
runtime = module("check_runtime_package")
contract = module("check_update_release_contract")


class PackagingSafetyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def put(self, name, content=b"fixture"):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
        return path

    def pe(self, machine=0x8664):
        header = bytearray(64)
        header[:2] = b"MZ"
        struct.pack_into("<I", header, 60, 64)
        return header + b"PE\0\0" + struct.pack("<H", machine)

    def fixture_runtime(self):
        for name in runtime.REQUIRED:
            self.put(name, self.pe() if name.endswith((".exe", ".dll")) else b"fixture")

    def test_source_archive_excludes_private_and_all_build_variants(self):
        good = {"src/main.cpp", "cmake/Test.cmake", "CMakeLists.txt"}
        bad = {"private-data/live.db", "dist-stage4/CarServicePointG.exe",
               "build-single-instance-audit/tests/private.sqlite", ".codex/session.json",
               "tmp/export.pdf", "src/.env.production", "resources/certificate.pfx",
               "resources/db.sqlite-wal", "resources/logs/session.txt"}
        for name in good | bad:
            self.put(name)
        with patch.object(archive, "ROOT", self.root):
            self.assertEqual(archive.main(["source.zip"]), 0)
        with zipfile.ZipFile(self.root / "source.zip") as result:
            self.assertEqual(set(result.namelist()), good)

    def test_archive_does_not_overwrite_or_escape_root(self):
        old = self.put("source.zip", b"preserved")
        with patch.object(archive, "ROOT", self.root):
            with self.assertRaises(FileExistsError):
                archive.main(["source.zip"])
            with self.assertRaises(ValueError):
                archive.main(["../escape.zip"])
        self.assertEqual(old.read_bytes(), b"preserved")

    def test_runtime_manifest_roundtrip_and_tamper(self):
        self.fixture_runtime()
        runtime.check(self.root, True)
        runtime.check(self.root)
        self.put("resources/assets/icon.png", b"unexpected addition")
        with self.assertRaisesRegex(ValueError, "mismatch"):
            runtime.check(self.root)

    def test_runtime_rejects_missing_private_scripts_and_wrong_architecture(self):
        self.fixture_runtime()
        for name in ("live.sqlite", ".env", "scripts/deploy_server.ps1",
                     "resources/assets/client.db", "resources/windows/ocr-layer/logs/secret.txt"):
            path = self.put(name)
            with self.assertRaises(ValueError):
                runtime.inventory(self.root)
            path.unlink()
            # Remove only the empty fixture directories, never project files.
            while path.parent != self.root:
                path = path.parent
                try:
                    path.rmdir()
                except OSError:
                    break
        self.put("Qt6Core.dll", self.pe(0x14c))
        with self.assertRaisesRegex(ValueError, "Non-x64"):
            runtime.inventory(self.root)
        self.put("Qt6Core.dll", self.pe())
        (self.root / "Qt6Gui.dll").unlink()
        with self.assertRaisesRegex(ValueError, "missing"):
            runtime.inventory(self.root)

    def test_runtime_rejects_modified_and_removed_files(self):
        self.fixture_runtime()
        self.put("resources/assets/icon.png")
        runtime.check(self.root, True)
        self.put("resources/assets/icon.png", b"changed")
        with self.assertRaisesRegex(ValueError, "mismatch"):
            runtime.check(self.root)
        (self.root / "resources/assets/icon.png").unlink()
        with self.assertRaisesRegex(ValueError, "mismatch"):
            runtime.check(self.root)

    def test_runtime_manifest_cannot_be_overwritten(self):
        self.fixture_runtime()
        runtime.check(self.root, True)
        previous = (self.root / runtime.MANIFEST).read_bytes()
        with self.assertRaises(FileExistsError):
            runtime.check(self.root, True)
        self.assertEqual((self.root / runtime.MANIFEST).read_bytes(), previous)

    def test_checksum_requires_exact_filename_hash_and_single_entry(self):
        digest = "a" * 64
        path = self.put("SHA256SUMS.txt")
        for content, valid in ((f"{digest}  setup.exe\n", True),
                               (f"{digest} *setup.exe\n", True),
                               (f"{digest}  other.exe\n", False),
                               (f"{'b' * 64}  setup.exe\n", False),
                               (f"{digest}  setup.exe\n" * 2, False),
                               ("not a checksum", False)):
            path.write_text(content, encoding="utf-8")
            self.assertEqual(contract.checksum_matches(path, "setup.exe", digest), valid)


if __name__ == "__main__":
    unittest.main()
