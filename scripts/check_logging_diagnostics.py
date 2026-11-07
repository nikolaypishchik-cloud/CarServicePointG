#!/usr/bin/env python3
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[1]
errors = []
logger_h = (ROOT / 'src/core/diagnostics/StructuredDiagnosticsLogger.h').read_text(encoding='utf-8')
logger_cpp = (ROOT / 'src/core/diagnostics/StructuredDiagnosticsLogger.cpp').read_text(encoding='utf-8')
import_job = (ROOT / 'src/core/import/ImportJob.cpp').read_text(encoding='utf-8')
async_scope = (ROOT / 'src/runtime/AsyncThreadScope.h').read_text(encoding='utf-8')
workparts_diagnostics = (ROOT / 'src/ui/tabs/orders/WorkPartsDiagnosticsSupport.cpp').read_text(encoding='utf-8')
for marker in ['correlationId','ensureCorrelationId','kMaxBreadcrumbs = 100','breadcrumbsSnapshot','captureStackTrace','logException','qCInfo(csWorkPartsTraceLog)','qCWarning(csWorkPartsTraceLog)','qCCritical(csWorkPartsTraceLog)']:
    if marker not in logger_h + logger_cpp:
        errors.append(f'StructuredDiagnosticsLogger is missing marker: {marker}')
if 'event.correlationId' not in workparts_diagnostics:
    errors.append('WorkPartsDialog structured diagnostics must set/propagate correlationId')
for path in (ROOT / 'src/ui/tabs/orders').rglob('*'):
    if path.suffix not in {'.cpp','.h','.hpp','.cc','.cxx'}:
        continue
    text = path.read_text(encoding='utf-8', errors='ignore')
    if 'qInfo' in text:
        errors.append(f'raw qInfo remains in WorkParts UI logging path: {path.relative_to(ROOT)}')
for name, text in {'src/core/import/ImportJob.cpp': import_job, 'src/runtime/AsyncThreadScope.h': async_scope}.items():
    if 'StructuredDiagnosticsLogger::logException' not in text:
        errors.append(f'{name} must log exceptions through StructuredDiagnosticsLogger::logException')
    if 'failed_not_success' not in text:
        errors.append(f'{name} catch path must explicitly mark failed_not_success')
    if 'unknown_non_std_exception' not in text:
        errors.append(f'{name} catch(...) path must keep technicalDetails for unknown exceptions')
if errors:
    print('P2 logging diagnostics gate failed:')
    for error in errors:
        print(f' - {error}')
    sys.exit(1)
print('P2 logging diagnostics gate passed')
