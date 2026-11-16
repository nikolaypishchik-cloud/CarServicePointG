"""Run one partition of the compiled Qt suite with the standard diagnostics."""
from pathlib import Path
import re
import subprocess
import sys


def main():
    exe = Path(sys.argv[1]).resolve(strict=True)
    groups = ('reports', 'migration', 'canonical')
    group = sys.argv[2]
    inventory = subprocess.check_output([str(exe), '-functions'], encoding='utf-8', errors='replace')
    names = re.findall(r'^([A-Za-z_]\w*)\(\)\s*$', inventory, re.M)
    if not names or len(names) != len(set(names)):
        raise ValueError('Missing or duplicate QtTest functions')
    parts = {key: [] for key in groups}
    for name in names:
        key = next((prefix for prefix in groups[1:] if name.startswith(prefix)), 'reports')
        parts[key].append(name)
    if not all(parts.values()):
        raise ValueError('Missing report, migration or canonical coverage')
    print(f'{group}: {len(parts[group])} of {len(names)} functions, including all data rows', flush=True)
    runner = Path(__file__).with_name('run_qt_test.py')
    return subprocess.call([sys.executable, '-B', str(runner), str(exe), *parts[group]])


if __name__ == '__main__':
    sys.exit(main())
