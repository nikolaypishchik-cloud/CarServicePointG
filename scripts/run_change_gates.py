"""Fail-closed change acceptance: static checks, fresh build, complete CTest run."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
STATIC = ('check_architecture.py', 'check_structure_contract.py', 'check_p2_acceptance_tests.py', 'check_legacy_marker_ci_gate.py',
          'check_runtime_blocking_gate.py', 'check_sql_safety_gate.py',
          'check_sql_exec_accountability.py', 'check_required_sources.py',
          'check_domain_integrity.py', 'check_test_inventory.py',
          'test_architecture_ratchet.py', 'test_structure_contract.py',
          'test_project_source_scope.py', 'test_qt_test_diagnostics.py', 'test_purchase_acceptance.py')
REQUIRED_TESTS = {'PurchasePaymentTests', 'SettingsLayoutTests', 'HistoryPartsAutofillTests',
                  'PartsQueryServiceTests', 'BackupRecoveryAcceptanceTests', 'OrderClosureDialogTests'}

def cache_errors(text):
    values = {}
    for line in text.splitlines():
        if '=' in line and ':' in line.split('=', 1)[0]:
            key, value = line.split('=', 1)
            values[key.split(':', 1)[0]] = value
    errors = []
    for key, expected in {'CS_ENFORCE_QUALITY_GATES': 'ON',
                          'CS_REMEDIATION_TEST_BUILD': 'OFF', 'BUILD_TESTING': 'ON'}.items():
        if values.get(key) != expected:
            errors.append(f'{key} must be {expected}')
    return errors

def junit_errors(path, expected=None):
    root = ET.parse(path).getroot()
    cases = list(root.iter('testcase'))
    if not cases:
        return ['CTest produced no test cases']
    errors = []
    if expected is not None and {case.get('name') for case in cases} != expected:
        errors.append('Test report does not match the complete configured test inventory')
    for case in cases:
        name = case.get('name', '<unnamed>')
        if any(case.find(tag) is not None for tag in ('failure', 'error', 'skipped')):
            errors.append(f'{name}: failed or skipped')
        # QtTest may return success while individual slots are skipped.
        output = '\n'.join(node.text or '' for node in case.iter('system-out'))
        if 'SKIP ' in output or 'SKIP   :' in output or 'QCRITICAL' in output or 'QFATAL' in output:
            errors.append(f'{name}: skipped slot or critical diagnostic; investigate before acceptance')
    return errors

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--static-only', action='store_true', help='Not a delivery approval')
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ctest', default='ctest')
    args = parser.parse_args()
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', PYTHONIOENCODING='utf-8')
    failures = []
    for name in STATIC:
        result = subprocess.run([sys.executable, '-B', str(ROOT/'scripts'/name)], cwd=ROOT, env=env)
        if result.returncode:
            failures.append(name)
    # An empty baseline cannot later become an unnoticed exemption list.
    baseline = json.loads((ROOT/'docs/architecture-baseline.json').read_text(encoding='utf-8'))
    if baseline.get('schema') != 1 or baseline.get('entries') != []:
        failures.append('architecture baseline must remain empty')
    if failures:
        print('CHANGE GATES BLOCKED: ' + ', '.join(failures))
        return 1
    if args.static_only:
        print('STATIC CHECKS ONLY: build, behavior and delivery are NOT accepted')
        return 0
    if args.build_dir is None:
        parser.error('--build-dir is required for acceptance')
    build = args.build_dir.resolve(strict=True)
    if build != (ROOT/'build-single-instance-audit').resolve():
        parser.error('Use the existing build-single-instance-audit directory')
    failures = cache_errors((build/'CMakeCache.txt').read_text(encoding='utf-8'))
    if failures:
        print('\n'.join(failures))
        return 1
    # Build ALL configured targets: a stale executable cannot certify new sources.
    if subprocess.run([args.cmake, '--build', str(build), '--config', 'Release'], cwd=ROOT, env=env).returncode:
        return 1
    inventory = subprocess.run([args.ctest, '--test-dir', str(build), '-C', 'Release', '--show-only=json-v1'],
                               cwd=ROOT, env=env, capture_output=True, text=True, check=True)
    tests = json.loads(inventory.stdout)['tests']
    names = {test['name'] for test in tests}
    if not REQUIRED_TESTS <= names or any(not test.get('command') for test in tests):
        print('Missing mandatory tests or executable commands')
        return 1
    profile = build/'change-gate-test-data'
    (profile/'temp').mkdir(parents=True, exist_ok=True)
    env.update(CS_TEST_DATA_ROOT=str(profile), TEMP=str(profile/'temp'), TMP=str(profile/'temp'),
               QT_QPA_PLATFORM='offscreen', CS_FINANCIAL_AUDIT='1')
    report = build/'change-gate-ctest.xml'
    if report.exists():
        report.unlink()  # Exact generated report, never application data.
    result = subprocess.run([args.ctest, '--test-dir', str(build), '-C', 'Release',
                             '--output-on-failure', '--no-tests=error', '--output-junit', str(report)],
                            cwd=ROOT, env=env)
    failures = junit_errors(report, names) if report.exists() else ['Missing fresh CTest report']
    if result.returncode or failures:
        print('\n'.join(failures))
        return 1
    print('CHANGE GATES PASSED: static + fresh build + complete registered CTest suite; not installer certification')
    return 0

if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, ET.ParseError, subprocess.SubprocessError) as error:
        print(f'CHANGE GATES INCOMPLETE: {error}', file=sys.stderr)
        raise SystemExit(1)
