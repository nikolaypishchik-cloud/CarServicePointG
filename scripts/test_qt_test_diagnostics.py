"""The delivery logger must fail closed, even when QtTest exits zero."""
import unittest
import re
from pathlib import Path
from run_qt_test import diagnostic_error


def access_test_has_diagnostic_runner(cmake):
    registration = re.search(r'add_test\(NAME PurchaseAccessRegressionTests\s+COMMAND\s+(.*?)\)', cmake, re.S)
    return bool(registration and re.match(
        r'\$\{CS_PYTHON_NO_BYTECODE_CMD\}\s+"\$\{CMAKE_SOURCE_DIR\}/scripts/run_qt_test.py"\s+\$<TARGET_FILE:PurchasePaymentTests>',
        registration.group(1)))


class DiagnosticsTests(unittest.TestCase):
    def test_access_registration_cannot_bypass_diagnostics(self):
        cmake = (Path(__file__).resolve().parents[1] / 'cmake/modules/CarServiceTests.cmake').read_text(encoding='utf-8')
        self.assertTrue(access_test_has_diagnostic_runner(cmake))
        self.assertFalse(access_test_has_diagnostic_runner(cmake.replace(
            '"${CMAKE_SOURCE_DIR}/scripts/run_qt_test.py"', '"unverified_runner.py"')))
        self.assertFalse(access_test_has_diagnostic_runner(cmake.replace(
            '${CS_PYTHON_NO_BYTECODE_CMD}', '')))
        self.assertFalse(access_test_has_diagnostic_runner(
            'add_test(NAME PurchaseAccessRegressionTests COMMAND $<TARGET_FILE:PurchasePaymentTests>)'))

    def test_rejects_critical_failure_and_skip(self):
        for marker in ('FAIL!', 'QCRITICAL', 'QFATAL', 'SKIP   :'):
            with self.subTest(marker=marker):
                self.assertTrue(diagnostic_error('PASS\n' + marker + ' injected\nTotals: 1 passed'))

    def test_clean_report(self):
        self.assertFalse(diagnostic_error('PASS   : example\nTotals: 1 passed, 0 failed, 0 skipped'))


if __name__ == '__main__':
    unittest.main()
