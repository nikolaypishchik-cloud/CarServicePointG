#!/usr/bin/env python3
import sys
sys.dont_write_bytecode = True
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TEST_MODULE = ROOT / 'cmake/modules/CarServiceTests.cmake'
CMAKE = ROOT / 'CMakeLists.txt'
WORKFLOW = ROOT / '.github/workflows/structure-contract.yml'

required = {
    'tests/unit/P2AcceptanceUnitTests.cpp': [
        'partDatePolicyServiceUnitGuard',
        'partRowPayloadBuilderUnitGuard',
        'importParserUnitGuard',
        'workPartsDirtyStateUnitGuard',
        'attachmentServiceUnitGuard',
        'updateHashAndPathLogicUnitGuard',
    ],
    'tests/integration/P2AcceptanceIntegrationTests.cpp': [
        'sqliteTransactionRollbackIntegrationGuard',
        'importedPartsEditSaveReopenIntegrationGuard',
        'invoiceImportWithoutExcelIntegrationGuard',
        'backupRecoveryAfterSaveIntegrationGuard',
        'updatePackageApplyRollbackIntegrationGuard',
    ],
    'tests/smoke/P2AcceptanceSmokeTests.cpp': [
        'excelLikeNavigationSmokeGuard',
        'importedPartEditingSmokeGuard',
        'rapidKeyboardNavigationSmokeGuard',
        'dialogCloseDuringImportSmokeGuard',
        'partsPageDuringRefreshSmokeGuard',
    ],
    'tests/acceptance/P2AcceptanceCriteriaTests.cpp': [
        'architectureAcceptanceCriteriaGuard',
        'importAndAttachmentAcceptanceCriteriaGuard',
        'importedPartAndEditorAcceptanceCriteriaGuard',
        'updateAndP0StabilityAcceptanceCriteriaGuard',
    ],
}

errors = []

registration_text = ''
for path in (TEST_MODULE, CMAKE):
    if path.exists():
        registration_text += path.read_text(encoding='utf-8', errors='ignore') + '\n'

for rel, markers in required.items():
    path = ROOT / rel
    if not path.exists():
        errors.append(f'missing required P2 acceptance test: {rel}')
        continue
    text = path.read_text(encoding='utf-8', errors='ignore')
    if 'QTEST_MAIN' not in text:
        errors.append(f'{rel}: missing QTEST_MAIN')
    if rel not in registration_text:
        errors.append(f'{rel}: not registered in CMake test module')
    for marker in markers:
        if marker not in text:
            errors.append(f'{rel}: missing test marker {marker}')

unit_text = (ROOT / 'tests/unit/P2AcceptanceUnitTests.cpp').read_text(encoding='utf-8', errors='ignore') if (ROOT / 'tests/unit/P2AcceptanceUnitTests.cpp').exists() else ''
integration_text = (ROOT / 'tests/integration/P2AcceptanceIntegrationTests.cpp').read_text(encoding='utf-8', errors='ignore') if (ROOT / 'tests/integration/P2AcceptanceIntegrationTests.cpp').exists() else ''

unit_requirements = [
    'PartDatePolicyService',
    'PartRowPayloadBuilderService',
    'ElitParser',
    'AvtonovaParser',
    'GenericXlsParser',
    'GenericPdfParser',
    'm_dirtyPartRows',
    'OrderAttachmentService',
    'computeFileSha256',
    'hash.addData(&f)',
]
for marker in unit_requirements:
    if marker not in unit_text:
        errors.append(f'P2 unit acceptance coverage missing marker: {marker}')

integration_requirements = [
    'batchSaveWorksAndPartsCommitsAtomically',
    'rollbackOnAttachmentStageFailureLeavesDirtyDataUncommitted',
    'databaseLockedSimulationReportsFailure',
    'importEditSaveReopenPathKeepsImportedPartStateAndDirtyRowsSafe',
    'pdfInvoiceWithoutExcelScenarioIsCovered',
    'xlsInvoiceWithoutInstalledExcelScenarioIsCovered',
    'BackupManagerCriticalFlowTests',
    'RecoveryConflictResolutionTests',
    'restart_old',
    'restore_backup',
]
for marker in integration_requirements:
    if marker not in integration_text:
        errors.append(f'P2 integration acceptance coverage missing marker: {marker}')

smoke_text = (ROOT / 'tests/smoke/P2AcceptanceSmokeTests.cpp').read_text(encoding='utf-8', errors='ignore') if (ROOT / 'tests/smoke/P2AcceptanceSmokeTests.cpp').exists() else ''
criteria_text = (ROOT / 'tests/acceptance/P2AcceptanceCriteriaTests.cpp').read_text(encoding='utf-8', errors='ignore') if (ROOT / 'tests/acceptance/P2AcceptanceCriteriaTests.cpp').exists() else ''

smoke_requirements = [
    'RegressionP1InlineEditorNavigationCoverageTests.cpp',
    'RegressionWorkPartsP0ImportedPartsTests.cpp',
    'SmokeWorkPartsDialogTests.cpp',
    'SmokeImportPathTests.cpp',
    'RegressionP1AsyncCancellationLifecycleTests.cpp',
    'RegressionP1PartsPageIncrementalRefreshTests.cpp',
    'Qt::Key_Return',
    'Qt::Key_Tab',
    'Qt::Key_Left',
    'Qt::Key_Right',
    'Qt::Key_Up',
    'Qt::Key_Down',
    'importedPartDateEditPreservesBlankOrderedDateAndSeparatesInvoiceDate',
    'importedPartNameBrandAndPriceEditUseCanonicalSavePayload',
    'invoiceOwnerCancellationWired',
    'invoiceCancellationRejectsBrokenLinks',
    'm_refreshQueued = true',
]
for marker in smoke_requirements:
    if marker not in smoke_text:
        errors.append(f'P2 UI smoke acceptance coverage missing marker: {marker}')

criteria_requirements = [
    'UI layer must not call Database::db() directly',
    'UI layer must not use Qt SQL query/database primitives directly',
    'UI layer must not block on QProcess::waitForFinished()',
    'queued_after_db_commit',
    'attachment_save_failed_dirty_kept',
    'payloadInput.preserveEmptyOrderedAt = importedInvoiceRow',
    'transfer_to_order_viewmodel',
    'queuedEditorActionStillValid',
    'csTableRevision',
    'restart_old',
    'restore_backup',
    'P0_STABLE_REPEAT_COUNT = 20',
]
for marker in criteria_requirements:
    if marker not in criteria_text:
        errors.append(f'P2 readiness acceptance criteria coverage missing marker: {marker}')

cmake_text = CMAKE.read_text(encoding='utf-8', errors='ignore') if CMAKE.exists() else ''
if 'p2_acceptance_tests_gate' not in cmake_text:
    errors.append('CMakeLists.txt must wire p2_acceptance_tests_gate into build dependencies')

workflow_text = WORKFLOW.read_text(encoding='utf-8') if WORKFLOW.exists() else ''
runner_text = (ROOT / 'scripts/run_change_gates.py').read_text(encoding='utf-8')
import re
if not re.search(r'^\s+run: python -B scripts/run_change_gates.py --static-only\s*$', workflow_text, re.M):
    errors.append('CI workflow must run static acceptance')
if "'check_p2_acceptance_tests.py'" not in runner_text.split('REQUIRED_TESTS', 1)[0]:
    errors.append('Static acceptance must include the P2 acceptance gate')

if errors:
    print('P2 tests/acceptance gate failed:')
    for err in errors:
        print(' -', err)
    sys.exit(1)

print('P2 tests/acceptance gate passed')
