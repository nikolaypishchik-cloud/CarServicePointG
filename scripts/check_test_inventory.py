#!/usr/bin/env python3
import sys
sys.dont_write_bytecode = True
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CMAKE = ROOT / 'CMakeLists.txt'
TEST_MODULE = ROOT / 'cmake/modules/CarServiceTests.cmake'
HELPER = ROOT / 'tests/TestSourceAssertions.h'
required = [
    Path('tests/OrderDetailIdentityTests.cpp'),
    Path('tests/OrderIdentityResolverTests.cpp'),
    Path('tests/OrderDetailsRepositoryTests.cpp'),
    Path('tests/LegacyCanonicalAncillaryImportSupportTests.cpp'),
    Path('tests/OrderWorkspaceServiceTests.cpp'),
    Path('tests/OrderIdentityServiceTests.cpp'),
    Path('tests/OrderMutationFacadeTests.cpp'),
    Path('tests/PartMutationServiceTests.cpp'),
    Path('tests/CurrentOrdersQueryServiceTests.cpp'),
    Path('tests/PartsQueryServiceTests.cpp'),
    Path('tests/BackupManagerCriticalFlowTests.cpp'),
    Path('tests/RecoveryConflictResolutionTests.cpp'),
    Path('tests/smoke/SmokeMainWindowTests.cpp'),
    Path('tests/smoke/SmokeCurrentOrdersPageTests.cpp'),
    Path('tests/smoke/SmokeOrderDialogTests.cpp'),
    Path('tests/smoke/SmokeWorkPartsDialogTests.cpp'),
    Path('tests/smoke/SmokeImportPathTests.cpp'),
    Path('tests/smoke/SmokeBackupRecoveryPathTests.cpp'),
    Path('tests/smoke/SmokeSettingsPageTests.cpp'),
    Path('tests/smoke/SmokePlanningPageTests.cpp'),
    Path('tests/regression/RegressionOrderPartsSaveTests.cpp'),
    Path('tests/regression/RegressionCloseReopenLifecycleTests.cpp'),
    Path('tests/regression/RegressionImportInvoiceTests.cpp'),
    Path('tests/regression/RegressionPartDatesTests.cpp'),
    Path('tests/regression/RegressionMarkupRecalculationTests.cpp'),
    Path('tests/regression/RegressionRecoveryAfterCrashTests.cpp'),
    Path('tests/regression/RegressionTabSwitchAfterAsyncRefreshTests.cpp'),
    Path('tests/unit/P2AcceptanceUnitTests.cpp'),
    Path('tests/integration/P2AcceptanceIntegrationTests.cpp'),
]
missing = [str(p) for p in required if not (ROOT / p).exists()]
if missing:
    print('Missing internal test scaffolds:')
    for item in missing:
        print(' -', item)
    sys.exit(1)
if not HELPER.exists():
    print('Missing shared test helper:', HELPER)
    sys.exit(1)
cmake_text = CMAKE.read_text(encoding='utf-8', errors='ignore')
if TEST_MODULE.exists():
    cmake_text += '\n' + TEST_MODULE.read_text(encoding='utf-8', errors='ignore')
errors = []
for rel in required:
    text = (ROOT / rel).read_text(encoding='utf-8', errors='ignore')
    if 'Placeholder anti-regression test scaffold is present' in text:
        errors.append(f'{rel}: placeholder scaffold text still present')
    if 'requireContains(' not in text and 'requireAnyContains(' not in text and 'readSourceFile(' not in text:
        errors.append(f'{rel}: missing source-assertion checks')
    if rel.as_posix() not in cmake_text:
        errors.append(f'{rel}: not registered in CMakeLists.txt')
if errors:
    print('Internal test inventory failed:')
    for err in errors:
        print(' -', err)
    sys.exit(1)
print('Internal test inventory OK')
