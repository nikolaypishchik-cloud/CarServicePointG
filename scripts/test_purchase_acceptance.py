"""Negative controls for the approved isolated/external audit boundary."""
import json
from pathlib import Path
import unittest
from run_purchase_acceptance import partition, NATIVE
from run_qt_test import diagnostic_error

ROOT = Path(__file__).resolve().parents[1]


class PurchaseBoundaryTests(unittest.TestCase):
    def test_new_tests_are_mandatory(self):
        self.assertEqual(partition([NATIVE, 'financialIndependentDecimalOracle', 'newTest', 'audit'],
                                  [{'scenario':'audit'}]), ['financialIndependentDecimalOracle', 'newTest'])

    def test_required_coverage_cannot_disappear(self):
        with self.assertRaises(ValueError):
            partition([NATIVE, 'newTest'], [])
        with self.assertRaises(ValueError):
            partition([NATIVE, 'financialIndependentDecimalOracle', 'newTest'], [{'scenario':NATIVE}])

    def test_missing_duplicate_and_empty_inventory_rejected(self):
        for functions, audits in [([], []), ([NATIVE], [{'scenario':'removed'}]),
                                  ([NATIVE, 'test', 'test'], []),
                                  ([NATIVE, 'test'], [{'scenario':'test'}, {'scenario':'test'}]),
                                  (['test'], []), ([NATIVE], [])]:
            with self.assertRaises(ValueError):
                partition(functions, audits)

    def test_oracle_cannot_be_external(self):
        with self.assertRaises(ValueError):
            partition([NATIVE,'test','financialIndependentDecimalOracle'],
                      [{'scenario':'financialIndependentDecimalOracle'}])

    def test_all_external_scenarios_retained(self):
        rows=json.loads((ROOT/'tests/purchase_integration_audits.json').read_text())['audits']
        source=(ROOT/'tests/PurchasePaymentTests.cpp').read_text(encoding='utf-8')
        self.assertEqual(len(rows),20)
        for row in rows:
            self.assertIn('void '+row['scenario']+'()',source)
            self.assertTrue(row['required_environment'])

    def test_diagnostics_still_fail_closed(self):
        for message in ('FAIL!', 'SKIP   :', 'QCRITICAL', 'QFATAL'):
            self.assertTrue(diagnostic_error('Totals: 1 passed\n'+message))

    def test_registration_and_strict_runner_connected(self):
        cmake=(ROOT/'cmake/modules/CarServiceTests.cmake').read_text()
        runner=(ROOT/'scripts/run_purchase_acceptance.py').read_text()
        self.assertIn('scripts/run_purchase_acceptance.py',cmake)
        self.assertIn("scripts/run_qt_test.py",runner)
        self.assertIn("'test_purchase_acceptance.py'",(ROOT/'scripts/run_change_gates.py').read_text())


if __name__ == '__main__':
    unittest.main()
