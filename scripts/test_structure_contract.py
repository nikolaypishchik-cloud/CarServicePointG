"""Mutation tests: intentionally bad changes must be rejected."""
import tempfile
from pathlib import Path
import unittest
import check_structure_contract as gate
import run_change_gates as acceptance

class StructureContractTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root/'src').mkdir()

    def put(self, name, text=''):
        path = self.root/name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')
        return path

    def test_allowed_application_path(self):
        self.put('src/core/services/Read.h')
        self.put('src/ui/Page.cpp', '#include "core/services/Read.h"')
        self.assertEqual(gate.scan(self.root), [])

    def test_direct_relative_and_angle_includes_cannot_bypass(self):
        self.put('src/infrastructure/security/Store.h')
        for include in ('"infrastructure/security/Store.h"',
                        '"../infrastructure/security/Store.h"',
                        '<infrastructure/security/Store.h>'):
            with self.subTest(include=include):
                self.put('src/ui/Page.ipp', '#include '+include)
                self.assertTrue(gate.scan(self.root))

    def test_reverse_dependency(self):
        self.put('src/app/Context.h')
        self.put('src/core/service.cpp', '#include "app/Context.h"')
        self.assertTrue(gate.scan(self.root))

    def test_integrations_cannot_own_ui(self):
        self.put('src/ui/Dialog.h')
        self.put('src/integrations/Update.cpp', '#include "ui/Dialog.h"')
        self.assertTrue(gate.scan(self.root))

    def test_domain_cannot_acquire_io(self):
        self.put('src/core/finance/Money.h', '#include <QSqlDatabase>')
        self.assertTrue(gate.scan(self.root))

    def test_comments_are_not_dependencies(self):
        self.put('src/app/Context.h')
        self.put('src/core/service.cpp', '/*\n#include "app/Context.h"\n*/\n// #include "app/Context.h"')
        self.assertEqual(gate.scan(self.root), [])

    def test_raw_strings_are_not_includes(self):
        self.put('src/app/Context.h')
        self.put('src/core/service.cpp', 'const auto text=R"doc(\n#include "app/Context.h"\n)doc";')
        self.assertEqual(gate.scan(self.root), [])

    def test_macro_include_fails_closed(self):
        self.put('src/ui/Page.cpp', '#define TARGET "infrastructure/Db.h"\n#include TARGET')
        self.assertTrue(gate.scan(self.root))

    def test_missing_test_in_partial_report(self):
        path = self.put('report.xml', '<testsuite><testcase name="one"/></testsuite>')
        self.assertTrue(acceptance.junit_errors(path, {'one', 'two'}))

    def test_c_extension_and_unknown_directory(self):
        self.put('src/new_layer/hidden.c', 'int x;')
        self.assertTrue(gate.scan(self.root))

    def test_private_data_is_not_scanned_but_source_artifacts_fail(self):
        self.put('private-data/backup.db', 'not source')
        self.assertEqual(gate.scan(self.root), [])
        self.put('src/core/backup.db')
        self.assertTrue(gate.scan(self.root))

    def test_empty_source_is_not_a_pass(self):
        self.assertTrue(gate.scan(self.root/'absent'))

    def test_acceptance_rejects_disabled_gates_and_remediation(self):
        valid = 'CS_ENFORCE_QUALITY_GATES:BOOL=ON\nCS_REMEDIATION_TEST_BUILD:BOOL=OFF\nBUILD_TESTING:BOOL=ON'
        self.assertEqual(acceptance.cache_errors(valid), [])
        self.assertTrue(acceptance.cache_errors(valid.replace('GATES:BOOL=ON', 'GATES:BOOL=OFF')))
        self.assertTrue(acceptance.cache_errors(valid.replace('BUILD:BOOL=OFF', 'BUILD:BOOL=ON')))
        self.assertTrue(acceptance.cache_errors(''))

    def test_test_report_cannot_hide_skips_failures_or_empty_run(self):
        for content in ('<testsuites/>', '<testsuite><testcase name="x"><skipped/></testcase></testsuite>',
                        '<testsuite><testcase name="x"><failure/></testcase></testsuite>',
                        '<testsuite><testcase name="x"><system-out>QCRITICAL: error</system-out></testcase></testsuite>',
                        '<testsuite><testcase name="x"><system-out>SKIP   : slot</system-out></testcase></testsuite>'):
            with self.subTest(content=content):
                self.assertTrue(acceptance.junit_errors(self.put('report.xml', content)))
        self.assertEqual(acceptance.junit_errors(self.put('report.xml', '<testsuite><testcase name="ok"/></testsuite>')), [])

if __name__ == '__main__':
    unittest.main()
