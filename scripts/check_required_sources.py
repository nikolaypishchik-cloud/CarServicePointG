#!/usr/bin/env python3
from __future__ import annotations

import sys
sys.dont_write_bytecode = True
import pathlib, re

ROOT = pathlib.Path(__file__).resolve().parents[1]
CMAKE = ROOT / 'CMakeLists.txt'
CMAKE_MODULES = ROOT / 'cmake' / 'modules'

CRITICAL_FILES = [
    'src/infrastructure/db/sqlite/DbArchiveRoutingSupport.cpp',
    'src/infrastructure/db/sqlite/DbArchiveRoutingLocalSupport.h',
    'src/infrastructure/db/sqlite/DbArchiveRoutingStageSupport.cpp',
    'src/infrastructure/db/sqlite/DbArchiveRoutingStageSupport.h',
    'src/infrastructure/db/sqlite/DbImportSupport.cpp',
    'src/infrastructure/db/sqlite/DbImportOrderStateLocalSupport.h',
    'src/infrastructure/db/sqlite/DbCatalogImportLocalSupport.h',
    'src/infrastructure/db/sqlite/DbImportCatalogFromOrders.cpp',
    'src/infrastructure/db/sqlite/DbImportCatalogFromOrders.h',
    'src/core/services/orders/WorkPartsPersistenceRepositoryFacade.cpp',
    'src/core/services/orders/WorkPartsPersistenceRepositoryFacade.h',
    'src/core/services/legacyimport/LegacyImportVerifySupport.cpp',
    'src/core/services/legacyimport/LegacyImportExecutionSupport.cpp',
    'src/core/backup/BackupManager.cpp',
    'src/core/backup/BackupJobQueue.cpp',
    'src/app/DeferredStartupMaintenance.cpp',
    'src/app/IdleMaintenanceScheduler.cpp',
    'src/runtime/AsyncTaskHandle.h',
    'resources/resources.qrc',
]

CRITICAL_SOURCE_LIST = [
    'src/infrastructure/db/sqlite/DbArchiveRoutingSupport.cpp',
    'src/infrastructure/db/sqlite/DbImportSupport.cpp',
    'src/infrastructure/db/sqlite/DbArchiveRoutingStageSupport.cpp',
    'src/infrastructure/db/sqlite/DbImportCatalogFromOrders.cpp',
    'src/core/services/orders/WorkPartsPersistenceRepositoryFacade.cpp',
    'src/core/services/legacyimport/LegacyImportVerifySupport.cpp',
    'src/core/services/legacyimport/LegacyImportExecutionSupport.cpp',
    'src/core/backup/BackupManager.cpp',
    'src/core/backup/BackupJobQueue.cpp',
    'src/app/DeferredStartupMaintenance.cpp',
    'src/app/IdleMaintenanceScheduler.cpp',
]

REQUIRED_CMAKE_MODULES = [
    'cmake/modules/CarServiceSourcesCore.cmake',
    'cmake/modules/CarServiceSourcesInfrastructure.cmake',
    'cmake/modules/CarServiceSourcesIntegrations.cmake',
    'cmake/modules/CarServiceSourcesUi.cmake',
    'cmake/modules/CarServiceTests.cmake',
]


def fail(msg: str) -> None:
    print(f'[support_integrity_gate] ERROR: {msg}', file=sys.stderr)
    raise SystemExit(1)


def main() -> int:
    missing = [p for p in CRITICAL_FILES + REQUIRED_CMAKE_MODULES if not (ROOT / p).exists()]
    if missing:
        fail('Missing critical files:\n  - ' + '\n  - '.join(missing))

    cmake_text = CMAKE.read_text(encoding='utf-8', errors='replace')
    module_text = '\n'.join(p.read_text(encoding='utf-8', errors='replace') for p in CMAKE_MODULES.glob('*.cmake'))
    normalized = re.sub(r'#.*', '', cmake_text + '\n' + module_text)
    for rel in CRITICAL_SOURCE_LIST:
        if rel not in normalized:
            fail(f'Critical source is not wired in CMake module lists: {rel}')

    owners = {
        'src/infrastructure/db/sqlite/DbArchiveRoutingLocalSupport.h': 'DbArchiveRoutingSupport',
        'src/infrastructure/db/sqlite/DbArchiveRoutingStageSupport.cpp': 'DbArchiveRoutingSupport',
        'src/infrastructure/db/sqlite/DbImportOrderStateLocalSupport.h': 'DbImportSupport',
        'src/infrastructure/db/sqlite/DbCatalogImportLocalSupport.h': 'DbImportSupport',
        'src/core/services/orders/WorkPartsPersistenceRepositoryFacade.h': 'WorkPartsDialog persistence boundary',
    }
    for rel, owner in owners.items():
        text = (ROOT / rel).read_text(encoding='utf-8', errors='replace')
        if not text.strip():
            fail(f'Critical support file is empty: {rel} (owner: {owner})')

    print('[support_integrity_gate] OK: critical files exist and CMake module wiring is intact')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
