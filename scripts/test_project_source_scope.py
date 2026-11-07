"""Negative controls for source ownership and vendored size review."""
import tempfile
import unittest
import subprocess
import sys
import shutil
from pathlib import Path
from unittest.mock import patch
from project_source_scope import project_paths
from check_monolith_split_gate import size_error, VENDORED_HEADERS, ROOT

class SourceScopeTests(unittest.TestCase):
    def test_generated_and_private_trees_are_not_entered(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ['src/a.cpp', 'private-data/secret.txt',
                         'web-crm/node_modules/package/a.cpp',
                         'mobile-android/app/build/generated.cpp']:
                p = root/name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.touch()
            import project_source_scope as scope
            walk = scope.os.walk
            def guarded(path, **kwargs):
                self.assertNotIn('private-data', str(path))
                yield from walk(path, **kwargs)
            with patch.object(scope.os, 'walk', guarded):
                found = {p.relative_to(root).as_posix() for p in project_paths(root)}
            self.assertIn('src/a.cpp', found)
            self.assertFalse(any('secret' in p or 'generated.cpp' in p or 'package' in p for p in found))

    def test_source_artifacts_cannot_hide_under_generated_names(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            names = ['src/build/error.log', 'src/node_modules/forbidden.cpp',
                     'web-crm/src/error.tmp', 'mobile-android/app/src/error.bak']
            for name in names:
                p = root/name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.touch()
            found = {p.relative_to(root).as_posix() for p in project_paths(root)}
            self.assertTrue(set(names) <= found)

    def test_large_owned_and_unreviewed_vendor_headers_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)/'large.hpp'
            p.write_text('\n'*501)
            self.assertIsNotNone(size_error(p, 'src/core/large.hpp'))
            self.assertIsNotNone(size_error(p, 'src/thirdparty/new/large.hpp'))
            self.assertIsNotNone(size_error(p, next(iter(VENDORED_HEADERS))))

    def test_pinned_vendor_matches_exact_reviewed_content(self):
        for name in VENDORED_HEADERS:
            self.assertIsNone(size_error(ROOT/name, name))

    def test_layout_gate_rejects_artifact_in_owned_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/'scripts').mkdir()
            (root/'src').mkdir()
            for name in ['check_project_layout.py', 'project_source_scope.py']:
                shutil.copyfile(ROOT/'scripts'/name, root/'scripts'/name)
            def run():
                return subprocess.run([sys.executable, '-B', str(root/'scripts/check_project_layout.py')],
                                      capture_output=True, text=True)
            self.assertEqual(run().returncode, 0)
            (root/'src/error.log').touch()
            result = run()
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('forbidden temp/log artifact', result.stdout)

    def test_marker_growth_and_unlisted_source_still_rejected(self):
        import check_legacy_marker_ci_gate as gate
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root/'src/example.cpp'
            source.parent.mkdir()
            source.write_text('// legacy legacy\n')
            row = {'budget': {'legacy': 1}, 'controlled': False,
                   'reason': 'Historical schema adaptation with explicit evidence.'}
            with patch.object(gate, 'ROOT', root), \
                 patch.object(gate, 'parse_inventory_rows', return_value={'src/example.cpp': row}):
                self.assertTrue(any('budget exceeded' in e for e in gate.check_controlled_marker_inventory()))
            with patch.object(gate, 'ROOT', root), \
                 patch.object(gate, 'parse_inventory_rows', return_value={}):
                self.assertTrue(any('without ADR' in e for e in gate.check_controlled_marker_inventory()))

    def test_p2_gate_rejects_missing_ci_and_disconnected_runner(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for folder in ['scripts', 'tests/unit', 'tests/integration', 'tests/smoke',
                           'tests/acceptance', 'cmake/modules', '.github/workflows']:
                (root/folder).mkdir(parents=True, exist_ok=True)
            files = ['scripts/check_p2_acceptance_tests.py', 'scripts/run_change_gates.py',
                     'CMakeLists.txt', 'cmake/modules/CarServiceTests.cmake',
                     '.github/workflows/structure-contract.yml',
                     'tests/unit/P2AcceptanceUnitTests.cpp',
                     'tests/integration/P2AcceptanceIntegrationTests.cpp',
                     'tests/smoke/P2AcceptanceSmokeTests.cpp',
                     'tests/acceptance/P2AcceptanceCriteriaTests.cpp']
            for name in files:
                shutil.copyfile(ROOT/name, root/name)
            def run():
                return subprocess.run([sys.executable, '-B', str(root/'scripts/check_p2_acceptance_tests.py')],
                                      capture_output=True, text=True, encoding='utf-8')
            result = run()
            self.assertEqual(result.returncode, 0, result.stdout)
            workflow = root/'.github/workflows/structure-contract.yml'
            workflow.unlink()
            self.assertNotEqual(run().returncode, 0)
            shutil.copyfile(ROOT/'.github/workflows/structure-contract.yml', workflow)
            runner = root/'scripts/run_change_gates.py'
            runner.write_text(runner.read_text(encoding='utf-8').replace("'check_p2_acceptance_tests.py'", "'removed.py'"), encoding='utf-8')
            self.assertNotEqual(run().returncode, 0)

if __name__ == '__main__':
    unittest.main()
