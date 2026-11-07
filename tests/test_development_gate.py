"""Isolation tests for the opt-in build diagnostic ratchet."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
class DevelopmentGateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "scripts").mkdir()
        (self.root / "docs").mkdir()
        shutil.copy2(ROOT / "scripts/run_development_gate.py", self.root / "scripts/run_development_gate.py")
        self.script = self.root / "scripts/check_project_layout.py"
        self.script.write_text("from pathlib import Path\nimport sys\ns=Path('diagnostics.txt').read_text()\nprint(s)\nsys.exit(3 if 'CRASH' in s else 1 if s.strip() else 0)\n")
        self.data = self.root / "diagnostics.txt"
        self.data.write_text(" - known diagnostic")
        self.assertEqual(self.run_gate("--record").returncode, 0)
    def run_gate(self, *args):
        return subprocess.run([sys.executable, "-B", str(self.root / "scripts/run_development_gate.py"), "check_project_layout.py", *args], cwd=self.root, capture_output=True, text=True)
    def test_unchanged_diagnostic_passes(self):
        self.assertEqual(self.run_gate().returncode, 0)
    def test_new_diagnostic_fails(self):
        self.data.write_text(" - known diagnostic\n - new diagnostic")
        self.assertNotEqual(self.run_gate().returncode, 0)
    def test_changed_checker_fails(self):
        self.script.write_text(self.script.read_text() + "\n# changed checker\n")
        self.assertNotEqual(self.run_gate().returncode, 0)
    def test_crash_cannot_be_accepted(self):
        self.data.write_text(" - known diagnostic\nCRASH")
        self.assertNotEqual(self.run_gate().returncode, 0)
    def test_resolved_diagnostic_requires_removal(self):
        self.data.write_text("")
        self.assertNotEqual(self.run_gate().returncode, 0)
    def test_cannot_overwrite_recorded_list(self):
        before=(self.root / "docs/development-baseline-check_project_layout.json").read_bytes()
        self.assertNotEqual(self.run_gate("--record").returncode, 0)
        self.assertEqual((self.root / "docs/development-baseline-check_project_layout.json").read_bytes(),before)

if __name__ == "__main__": unittest.main()
