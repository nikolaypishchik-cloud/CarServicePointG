import importlib.util
import pathlib
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('gate',pathlib.Path(__file__).with_name('check_architecture.py'))
gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)

class RatchetTests(unittest.TestCase):
    def test_ipp_and_previously_allowlisted_file_are_scanned(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp);(root/'ui/tabs/crm').mkdir(parents=True)
            (root/'ui/tabs/crm/CrmPage.cpp').write_text('QSqlQuery query;')
            (root/'ui/job.ipp').write_text('QSqlDatabase database;')
            found,_=gate.scan_occurrences(root,gate.DISALLOWED_RULES)
            self.assertEqual(len(found),2)
    def test_line_shift_stable_but_duplicate_is_new(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp);(root/'ui').mkdir();p=root/'ui/test.cpp'
            p.write_text('QSqlQuery query;')
            first,_=gate.scan_occurrences(root,gate.DISALLOWED_RULES)
            p.write_text('\n\nQSqlQuery query;')
            shifted,_=gate.scan_occurrences(root,gate.DISALLOWED_RULES)
            self.assertEqual(first[0]['fingerprint'],shifted[0]['fingerprint'])
            p.write_text('QSqlQuery query;\nQSqlQuery query;')
            duplicates,_=gate.scan_occurrences(root,gate.DISALLOWED_RULES)
            self.assertEqual(len({v['fingerprint'] for v in duplicates}),2)
    def test_new_call_in_same_file_is_not_accepted(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp);(root/'ui').mkdir();p=root/'ui/test.cpp'
            p.write_text('QSqlQuery oldQuery;')
            first,_=gate.scan_occurrences(root,gate.DISALLOWED_RULES)
            p.write_text('QSqlQuery oldQuery;\nQSqlQuery newQuery;')
            second,_=gate.scan_occurrences(root,gate.DISALLOWED_RULES)
            self.assertEqual(len({v['fingerprint'] for v in second}-{v['fingerprint'] for v in first}),1)

if __name__=='__main__':unittest.main()
