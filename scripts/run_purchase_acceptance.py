"""Isolated PurchasePayment acceptance; explicit, separately runnable external audits."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from financial_decimal_oracle import generate

ROOT = Path(__file__).resolve().parents[1]
NATIVE = 'popupCaptionsExcludeApplicationName'


def partition(functions, audits):
    names = [row['scenario'] for row in audits]
    if not functions or len(functions) != len(set(functions)):
        raise ValueError('Missing or duplicate compiled test inventory')
    if len(names) != len(set(names)) or not set(names) <= set(functions):
        raise ValueError('External audit declaration does not match compiled tests')
    if (NATIVE not in functions or 'financialIndependentDecimalOracle' not in functions
            or 'financialIndependentDecimalOracle' in names or NATIVE in names):
        raise ValueError('Mandatory native/independent oracle coverage missing')
    selected = [name for name in functions if name not in names and name != NATIVE]
    if not selected:
        raise ValueError('No isolated tests')
    return selected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--audit', help='Explicit external audit; never enabled implicitly')
    args = parser.parse_args()
    exe = args.executable.resolve(strict=True)
    declaration = json.loads((ROOT/'tests/purchase_integration_audits.json').read_text(encoding='utf-8'))
    if declaration['schema'] != 1:
        raise ValueError('Unknown audit declaration schema')
    audits = declaration['audits']
    env = dict(os.environ)
    if not args.audit:
        # Do not inherit production audit paths, credentials or opt-in switches.
        root = env.get('CS_TEST_DATA_ROOT')
        env = {k: v for k, v in env.items() if not k.startswith('CS_')}
        if not root:
            root = str(ROOT/'build-single-instance-audit/change-gate-test-data')
        env.update(CS_TEST_DATA_ROOT=root, CS_FINANCIAL_AUDIT='1', QT_QPA_PLATFORM='offscreen')
        profile = Path(root).resolve()
        if not profile.is_relative_to((ROOT/'build-single-instance-audit').resolve()):
            raise ValueError('Test profile must be inside the audit build')
        (profile/'temp').mkdir(parents=True, exist_ok=True)
        env.update(TEMP=str(profile/'temp'), TMP=str(profile/'temp'))
    inventory = subprocess.run([str(exe), '-functions'], env=env, capture_output=True,
                               text=True, encoding='utf-8', errors='replace', check=True)
    functions = re.findall(r'^([A-Za-z_][A-Za-z_0-9]*)\(\)\s*$', inventory.stdout, re.M)
    selected = partition(functions, audits)
    runner = [sys.executable, '-B', str(ROOT/'scripts/run_qt_test.py'), str(exe), '-maxwarnings', '1000000']
    if args.audit:
        row = next((row for row in audits if row['scenario'] == args.audit), None)
        if not row:
            raise ValueError('Unknown external audit')
        missing = [key for key in row['required_environment'] if not env.get(key)]
        if missing:
            raise ValueError('Missing explicit audit prerequisites: ' + ', '.join(missing))
        return subprocess.run(runner + [args.audit], env=env, cwd=ROOT).returncode
    output = profile/'purchase-acceptance'
    output.mkdir(exist_ok=True)
    oracle = output/'oracle.json'
    oracle.write_text(json.dumps(generate(), indent=2), encoding='utf-8')
    env.update(CS_FINANCIAL_ORACLE=str(oracle), CS_FINANCIAL_ACTUAL=str(output/'actual.json'))
    report = {'isolated': selected, 'native': NATIVE, 'status': 'RUNNING',
              'external_audits': [dict(row, status='NOT_RUN_EXTERNAL') for row in audits]}
    def save():
        (output/'coverage.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    save()
    result = subprocess.run(runner + selected, env=env, cwd=ROOT).returncode
    report['isolated_exit'] = result
    if result == 0:
        env['QT_QPA_PLATFORM'] = 'windows'
        result = subprocess.run(runner + [NATIVE], env=env, cwd=ROOT).returncode
        report['native_exit'] = result
    report['status'] = 'PASS' if result == 0 else 'FAIL'
    save()
    print(f'Isolated/native acceptance: {report["status"]}; external audits NOT RUN: {len(audits)}; {output / "coverage.json"}')
    return result


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f'Purchase acceptance incomplete: {error}', file=sys.stderr)
        raise SystemExit(1)
