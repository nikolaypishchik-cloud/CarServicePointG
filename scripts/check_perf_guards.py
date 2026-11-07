#!/usr/bin/env python3
import sys
sys.dont_write_bytecode = True
from pathlib import Path


def _project_root() -> Path:
    # Do not use Path.resolve() here: on Windows SUBST/mapped drives it can
    # canonicalize X:\\ back to an old physical Cyrillic path and make CMake
    # gates read stale/missing files. Keep the build invocation path stable.
    script_root = Path(__file__).absolute().parents[1]
    cwd_root = Path.cwd().absolute()
    for candidate in (script_root, cwd_root):
        if (candidate / 'CMakeLists.txt').exists() and (candidate / 'src').exists():
            return candidate
    return script_root


root = _project_root()
errors = []


def _read_required(rel_path: str) -> str:
    path = root / rel_path
    try:
        return path.read_text(encoding='utf-8', errors='ignore')
    except FileNotFoundError:
        errors.append(f'missing required performance guard input: {rel_path}')
        return ''


def _read_first(*rel_paths: str) -> str:
    for rel_path in rel_paths:
        path = root / rel_path
        if path.exists():
            return path.read_text(encoding='utf-8', errors='ignore')
    errors.append('missing required performance guard input, expected one of: ' + ', '.join(rel_paths))
    return ''


# Root package should not contain nested release archives.
for item in root.iterdir():
    if item.is_file() and item.suffix.lower() == '.zip':
        errors.append(f'nested archive left in project root: {item.name}')

reports_h = _read_required('src/ui/tabs/reports/ReportsPage.h')
reports_flow = _read_required('src/ui/tabs/reports/ReportsPageRefreshFlow.cpp')
async_cpp = _read_required('src/ui/tabs/reports/ReportsAsyncSupport.cpp')
parts_h = _read_required('src/ui/tabs/parts/PartsPage.h')
parts_cpp = _read_required('src/ui/tabs/parts/PartsPage.cpp')
parts_runtime = _read_first(
    'src/ui/tabs/parts/PartsPageRuntime.cpp',
    'src/ui/tabs/parts/PartsPageRuntime.ipp',
)
stats_h = _read_required('src/ui/tabs/statistics/StatsPage.h')
stats_cpp = _read_required('src/ui/tabs/statistics/StatsPage.cpp')

if 'void closeEvent(QCloseEvent* event) override;' not in reports_h:
    errors.append('ReportsPage is missing closeEvent async-cancel override')
if 'AsyncTaskHandle::renewCancellationToken(m_asyncCancelToken)' not in reports_flow:
    errors.append('ReportsPage refresh flow is missing cancellation token renewal')
if 'const AsyncTaskHandle& asyncHandle' not in async_cpp:
    errors.append('ReportsAsyncSupport workers are not wired to AsyncTaskHandle contract')

if 'void closeEvent(QCloseEvent* event) override;' not in parts_h:
    errors.append('PartsPage is missing closeEvent async-cancel override')
if 'AsyncTaskHandle::cancel(m_refreshCancelToken);' not in parts_cpp:
    errors.append('PartsPage is missing cancel on close/destruction')
if 'AsyncTaskHandle::renewCancellationToken(m_refreshCancelToken)' not in parts_runtime:
    errors.append('PartsPage async refresh is missing cancellation token renewal')

if 'void closeEvent(QCloseEvent* event) override;' not in stats_h:
    errors.append('StatsPage is missing closeEvent async-cancel override')
if 'AsyncTaskHandle::cancel(m_refreshCancelToken);' not in stats_cpp:
    errors.append('StatsPage is missing cancel on close/destruction')
if 'AsyncTaskHandle::renewCancellationToken(m_refreshCancelToken)' not in stats_cpp:
    errors.append('StatsPage async refresh is missing cancellation token renewal')

if errors:
    for err in errors:
        print(f'[P4] {err}', file=sys.stderr)
    sys.exit(1)
print('P4 performance/async guard check passed.')
