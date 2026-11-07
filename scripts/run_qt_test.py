"""Expose Windows QtTest file diagnostics to CTest, including crashes and skips."""
from pathlib import Path
import subprocess
import sys
import tempfile


def diagnostic_error(text):
    return any(marker in text for marker in ('FAIL!', 'QCRITICAL', 'QFATAL', 'SKIP   :'))


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    executable = Path(sys.argv[1]).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix=executable.stem + '-log-') as directory:
        report = Path(directory) / 'result.txt'
        result = subprocess.run([str(executable), '-o', str(report) + ',txt', *sys.argv[2:]])
        if not report.exists():
            print('Missing QtTest diagnostic report', flush=True)
            return 1
        text = report.read_text(encoding='utf-8', errors='replace')
        print(text, flush=True)
        if result.returncode or diagnostic_error(text) or 'Totals:' not in text:
            return 1
        return 0


if __name__ == '__main__':
    raise SystemExit(main())
