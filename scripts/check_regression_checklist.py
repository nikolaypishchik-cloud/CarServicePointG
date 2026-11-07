#!/usr/bin/env python3
from __future__ import annotations

import sys
sys.dont_write_bytecode = True

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHECKS = {
    'launch': ROOT / 'tests' / 'smoke' / 'SmokeMainWindowTests.cpp',
    'create_edit_order': ROOT / 'tests' / 'smoke' / 'SmokeOrderDialogTests.cpp',
    'workparts': ROOT / 'tests' / 'smoke' / 'SmokeWorkPartsDialogTests.cpp',
    'import_invoice': ROOT / 'tests' / 'regression' / 'RegressionImportInvoiceTests.cpp',
    'reports': ROOT / 'src' / 'ui' / 'tabs' / 'reports' / 'ReportsPage.cpp',
    'parts_sorting': ROOT / 'src' / 'ui' / 'tabs' / 'parts' / 'PartsPageRuntime.cpp',
    'settings_open': ROOT / 'tests' / 'smoke' / 'SmokeSettingsPageTests.cpp',
    'planning': ROOT / 'tests' / 'smoke' / 'SmokePlanningPageTests.cpp',
    'backup_restore': ROOT / 'tests' / 'smoke' / 'SmokeBackupRecoveryPathTests.cpp',
    'close_reopen': ROOT / 'tests' / 'regression' / 'RegressionCloseReopenLifecycleTests.cpp',
}

missing = [name for name, path in CHECKS.items() if not path.exists()]
if missing:
    print('[regression_checklist_gate] ERROR: missing checklist coverage items:', file=sys.stderr)
    for name in missing:
        print(f' - {name}', file=sys.stderr)
    raise SystemExit(1)

print('[regression_checklist_gate] OK: launch/create-edit/reports/parts/settings/planning/backup/close-reopen checklist is wired')
