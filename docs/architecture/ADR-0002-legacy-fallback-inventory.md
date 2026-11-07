# ADR-0002 — Legacy / fallback / transitional tail inventory

## Status
Accepted — P2 cleanup inventory baseline.

## Scope
This inventory covers P2 **“Мертві хвости, legacy і fallback” п1–п2**. It records every source/archive text file that contains one of these controlled markers: `legacy`, `fallback`, `MissingMethods`, `transitional`, `allowlist`.

Generated checksum files are intentionally excluded from the semantic inventory because they mirror file names and hashes, not implementation logic. Third-party vendored Python code is listed as active/vendor-owned so it is not accidentally edited during application cleanup.

## Decision
No runtime code is deleted in this pass. P2 п1–п2 require inventory and status assignment only. Deletion is intentionally deferred until the compile-check and grep/call-graph phase described by the next items of the same ТЗ block.

## Status legend
- **active** — currently wired production/runtime/build behavior.
- **deprecated but used** — transitional shim, migration marker, or allowlist that is still referenced by build/tests.
- **test-only** — appears only in test coverage or test assertions.
- **delete candidate** — textual tail that needs manual call-graph review before deletion.
- **dead** — no confirmed dead runtime code was proven in this p1–p2 pass; no element is marked dead without call-graph proof.

## Summary
- Files inventoried: **283**
- Status counts: active: 267, deprecated but used: 2, test-only: 14

## Inventory table
| Path | Markers | Status | Action / reason |
|---|---:|---|---|
| `src/core/crm/CrmFinancialService.cpp` | legacy=2 | active | Read-only compatibility with persisted discount_mode=legacy: preserve historical UAH discount semantics from FormatUtils::discountAmount; no inferred percent, no migration or write. Covered by crmClientFinancialReadModel. |
| `CMakeLists.txt` | legacy=43, fallback=7, MissingMethods=1 | active | Registers existing compatibility gates and their mandatory release dependencies; no runtime compatibility branch is introduced. |
| `github_release_manifest.json` | fallback=1 | active | Release/update contract intentionally documents ProgramData/AppData update-state fallback; required for safe updater behavior. |
| `cmake/modules/CarServiceSourcesCore.cmake` | legacy=99 | active | Build graph reference to active source/test files. |
| `cmake/modules/CarServiceSourcesInfrastructure.cmake` | legacy=4 | active | Build graph reference to active source/test files. |
| `cmake/modules/CarServiceSourcesUi.cmake` | missingmethods=1 | active | Build graph reference to active source/test files. |
| `cmake/modules/CarServiceTests.cmake` | legacy=9, fallback=2 | active | Registers compatibility regression names, including legacyStatesAndPartialSettlement; keep executable coverage. |
| `resources/python_vendor/pypdf/_crypt_providers/__init__.py` | fallback=1 | active | Third-party/vendor fallback or legacy terminology; do not edit in app cleanup pass. |
| `resources/python_vendor/pypdf/_crypt_providers/_fallback.py` | fallback=1 | active | Third-party/vendor fallback or legacy terminology; do not edit in app cleanup pass. |
| `resources/python_vendor/pypdf/_page.py` | fallback=2, legacy=1 | active | Third-party/vendor fallback or legacy terminology; do not edit in app cleanup pass. |
| `resources/python_vendor/pypdf/_page_labels.py` | fallback=4 | active | Third-party/vendor fallback or legacy terminology; do not edit in app cleanup pass. |
| `resources/python_vendor/pypdf/filters.py` | fallback=1 | active | Third-party/vendor fallback or legacy terminology; do not edit in app cleanup pass. |
| `resources/python_vendor/pypdf/generic/_base.py` | fallback=1 | active | Third-party/vendor fallback or legacy terminology; do not edit in app cleanup pass. |
| `resources/python_vendor/pypdf/generic/_data_structures.py` | fallback=1 | active | Third-party/vendor fallback or legacy terminology; do not edit in app cleanup pass. |
| `resources/python_vendor/pypdf/generic/_image_inline.py` | legacy=2 | active | Third-party/vendor fallback or legacy terminology; do not edit in app cleanup pass. |
| `resources/python_vendor/pypdf/xmp.py` | fallback=1 | active | Third-party/vendor fallback or legacy terminology; do not edit in app cleanup pass. |
| `resources/windows/ocr-layer/README.md` | fallback=1 | active | Windows OCR fallback documentation/runtime bundle remains required for invoice import. |
| `resources/windows/ocr-layer/README_100_PERCENT_PDF_IMPORT_RUNTIME.md` | fallback=1 | active | Windows OCR/PDF runtime documentation for the offline import fallback chain; required for invoice import deployment. |
| `resources/windows/ocr-layer/manifest.json` | fallback=1 | active | Windows OCR/PDF runtime manifest lists portable fallback engines packaged with the application. |
| `resources/windows/ocr-layer/python/site-packages/pypdf/_crypt_providers/__init__.py` | fallback=1 | active | Third-party/vendor fallback or legacy terminology; do not edit in app cleanup pass. |
| `resources/windows/ocr-layer/python/site-packages/pypdf/_crypt_providers/_fallback.py` | fallback=1 | active | Third-party/vendor fallback or legacy terminology; do not edit in app cleanup pass. |
| `resources/windows/ocr-layer/python/site-packages/pypdf/_page.py` | fallback=2, legacy=1 | active | Third-party/vendor fallback or legacy terminology; do not edit in app cleanup pass. |
| `resources/windows/ocr-layer/python/site-packages/pypdf/_page_labels.py` | fallback=4 | active | Third-party/vendor fallback or legacy terminology; do not edit in app cleanup pass. |
| `resources/windows/ocr-layer/python/site-packages/pypdf/filters.py` | fallback=1 | active | Third-party/vendor fallback or legacy terminology; do not edit in app cleanup pass. |
| `resources/windows/ocr-layer/python/site-packages/pypdf/generic/_base.py` | fallback=1 | active | Third-party/vendor fallback or legacy terminology; do not edit in app cleanup pass. |
| `resources/windows/ocr-layer/python/site-packages/pypdf/generic/_data_structures.py` | fallback=1 | active | Third-party/vendor fallback or legacy terminology; do not edit in app cleanup pass. |
| `resources/windows/ocr-layer/python/site-packages/pypdf/generic/_image_inline.py` | legacy=2 | active | Third-party/vendor fallback or legacy terminology; do not edit in app cleanup pass. |
| `resources/windows/ocr-layer/python/site-packages/pypdf/xmp.py` | fallback=1 | active | Third-party/vendor fallback or legacy terminology; do not edit in app cleanup pass. |
| `scripts/check_architecture.py` | allowlist=7, transitional=7 | active | Governance/build gate script or controlled inventory input. |
| `scripts/check_domain_integrity.py` | legacy=11 | active | Governance/build gate script or controlled inventory input. |
| `scripts/check_legacy_tail_inventory.py` | legacy: controlled, fallback: controlled, MissingMethods: controlled, transitional: controlled, allowlist: controlled | active | Governance/build gate script or controlled inventory input. |
| `scripts/check_ocr_runtime.ps1` | fallback=1 | active | Build/deployment validation script for bundled OCR/PDF fallback engines. |
| `scripts/check_part_pricing_tz.py` | legacy=3, fallback=2 | active | Governance gate intentionally scans pricing fallback and legacy metadata markers to prevent unreviewed pricing regressions. |
| `scripts/prepare_ocr_runtime_from_installers.ps1` | fallback=1 | active | Deployment helper that expands bundled OCR/PDF fallback installers into the app-local runtime layer before final installer build. |
| `scripts/check_naming_policy.py` | allowlist=1, missingmethods=1 | active | Governance/build gate script or controlled inventory input. |
| `scripts/check_required_sources.py` | legacy=8 | active | Governance/build gate script or controlled inventory input. |
| `scripts/check_runtime_conflicts.py` | allowlist=8, missingmethods=1, transitional=1 | active | Governance/build gate script or controlled inventory input. |
| `scripts/check_test_inventory.py` | legacy=1 | active | Governance/build gate script or controlled inventory input. |
| `scripts/p3_transitional_allowlist.txt` | allowlist=2, missingmethods=1, transitional=1 | deprecated but used | Explicit transitional allowlist; P2 p5 reduced it from 3 runtime entries to 1. |
| `scripts/p7_naming_allowlist.txt` | missingmethods=1 | deprecated but used | Naming policy allowlist; not the P2 p5 transitional-runtime budget source. |
| `docs/AUDIT_MARKUP_TZ_100_REPORT.md` | legacy=7, fallback=3 | active | Audit report documents controlled pricing compatibility and fallback behavior for the markup TZ closure; not runtime logic. |
| `docs/AUDIT_MARKUP_TZ_REPORT.md` | legacy=1, fallback=4 | active | Audit report documents controlled pricing fallback findings from the markup TZ audit; not runtime logic. |
| `docs/AUDIT_TZ_100_RUNTIME_BLOCKING_SQL_GATE_2026_05_13.txt` | legacy=3, fallback=0 | active | Audit report documents the final P0/P3 runtime blocking gate and compatibility import path status; not runtime logic. |
| `docs/hotfix/BUILD_FIX_EXCEL_RAW_STRING_FRAGMENT_2026_05_13.txt` | fallback=2 | active | Hotfix note documents the Excel raw-string split boundary correction; documentation-only, not runtime logic. |
| `docs/PERFORMANCE_TZ_100_COMPLETION_REPORT_2026_05_14.md` | fallback=1 | active | Performance remediation report documents controlled import denial behavior; documentation-only, not runtime logic. |
| `docs/AUDIT_PERFORMANCE_TZ_100PLUS_2026_05_14.txt` | legacy=3 | active | Performance remediation follow-up report references controlled gate script names; documentation-only, not runtime logic. |
| `docs/AUDIT_LEGACY_TAIL_INVENTORY_BUILD_FIX_2026_05_15.md` | legacy=6, fallback=3, transitional=1, allowlist=1 | active | Build-fix audit report documents controlled inventory gate terminology; documentation-only, not runtime logic. |
| `docs/AUDIT_PERFORMANCE_TZ_FINAL_100_TIMEOUT_GUARDS_2026_05_15.md` | legacy=3 | active | Performance remediation audit report references controlled legacy-tail gate terms; documentation-only, not runtime logic. |
| `docs/AUDIT_PERFORMANCE_TZ_REPORT_MODEL_CLOSURE_2026_05_15.md` | legacy=2 | active | Performance remediation audit report references controlled legacy-tail gate terms; documentation-only, not runtime logic. |
| `docs/AUDIT_PERFORMANCE_TZ_PARTS_ARCHIVE_MODEL_100_PASS_2026_05_15.md` | legacy=1 | active | Performance remediation audit report references controlled legacy-tail gate terms; documentation-only, not runtime logic. |
| `scripts/check_performance_tz_100_completion.py` | legacy=2 | active | Governance gate scans performance-TZ closure requirements and must remain inventoried for controlled legacy terminology. |
| `scripts/run_windows_performance_tz_acceptance.ps1` | legacy=1 | active | Windows acceptance runner documents controlled legacy-tail gate execution; build/acceptance helper only, not runtime logic. |
| `src/app/App.cpp` | legacy=7, fallback=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/app/App.h` | legacy=3 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/app/ClickSoundFilter.cpp` | fallback=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/app/StartupPipeline.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/backup/BackupDtos.h` | fallback=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/backup/BackupEngineState.h` | fallback=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/backup/BackupManager.cpp` | fallback=6 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/backup/BackupPaths.cpp` | fallback=4 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/backup/BackupSnapshotService.cpp` | fallback=6 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/import/InvoiceAttachmentPdfService.cpp` | fallback=4 | active | Invoice text recognition alternatives and QSaveFile direct-write refusal preserve atomic publication. |
| `src/core/import/InvoiceImportEngine.cpp` | legacy=6, fallback=73 | active | Еліт VAT-price parser intentionally adds guarded compatibility branches around existing import fallback parsing; keep budget synchronized with source until parser is split into smaller services. |
| `src/core/import/InvoiceImportEngine.h` | legacy=1, fallback=2 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/ActiveArchiveRoutingLayer.cpp` | fallback=2 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/ActiveArchiveRoutingLayer.h` | fallback=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/DatabaseImportClassifier.cpp` | legacy=13 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/DatabaseImportClassifier.h` | legacy=4 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/LegacyAncillaryMutationLogSupport.cpp` | legacy=7 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/LegacyAncillaryMutationLogSupport.h` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/LegacyMigrationService.cpp` | legacy=27 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/LegacyMigrationService.h` | legacy=18 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/LegacyMutationLogSupport.cpp` | legacy=10 | active | Snapshot now preserves historical discount_mode together with amounts in the existing mutation log. |
| `src/core/services/LegacyMutationLogSupport.h` | legacy=2 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/MutationExecutor.cpp` | fallback=2 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/MutationExecutor.h` | fallback=2 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/MutationPostCommitService.cpp` | legacy=2 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/OrderRoutingLookupService.cpp` | fallback=3 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/OrderStoragePolicyService.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/OrderWorkspaceService.cpp` | legacy=1, fallback=2 | active | Accepts explicit historical mode alongside percent and uah when saving existing orders. |
| `src/core/services/PartsQueryService.cpp` | legacy=11, fallback=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/PartOverduePolicy.cpp` | legacy=1 | active | Active compatibility rule keeps blank-status, unclosed historical order rows open-compatible until migrated-data regression tests prove the legacy condition can be removed safely. |
| `src/core/services/ReportsDataLoaderLocalSupport.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/ReportsDataLoaderSnapshotFlow.cpp` | legacy=2 | active | Reads historical discount mode in report snapshots instead of guessing units. |
| `src/core/services/ReportsDateUtils.cpp` | fallback=7 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/ReportsDateUtils.h` | fallback=3 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/ReportsPdfSupportLocalSupport.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/ReportsPdfSupportSnapshotFlow.cpp` | legacy=2 | active | Carries historical discount mode into payroll report calculation. |
| `src/core/services/ReportsSyncSupport.cpp` | legacy=6 | active | Preserves historical discount mode in report queries and existing expense mutation audit. |
| `src/core/services/catalog/CatalogAdminService.cpp` | legacy=3 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/catalog/CatalogQueryService.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/legacyimport/LegacyImportAcceptanceSupport.cpp` | legacy=16 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportAcceptanceSupport.h` | legacy=9 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportAncillaryAuditSupport.cpp` | legacy=9 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportAncillaryAuditSupport.h` | legacy=6 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportAncillarySchemaMapSupport.cpp` | legacy=15, fallback=9 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportAncillarySchemaMapSupport.h` | legacy=4 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportAncillaryVerifySupport.cpp` | legacy=15 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportAncillaryVerifySupport.h` | legacy=5 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportAuditSupport.cpp` | legacy=32 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportAuditSupport.h` | legacy=5 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportDetailedAuditSupport.cpp` | legacy=13 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportDetailedAuditSupport.h` | legacy=4 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportEntityAuditSupport.cpp` | legacy=10 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportEntityAuditSupport.h` | legacy=4 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportEntityVerifySupport.cpp` | legacy=9, fallback=2 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportEntityVerifySupport.h` | legacy=4 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportExecutionSupport.cpp` | legacy=117 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportExecutionSupport.h` | legacy=11 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportJournalSupport.cpp` | legacy=41 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportJournalSupport.h` | legacy=19 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportPolicySupport.cpp` | legacy=16 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportPolicySupport.h` | legacy=5 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportPostCheckSupport.cpp` | legacy=10 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportPostCheckSupport.h` | legacy=4 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportReportSupport.cpp` | legacy=77 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportReportSupport.h` | legacy=13 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportSafetySupport.cpp` | legacy=11 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportSafetySupport.h` | legacy=5 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportScenarioSupport.cpp` | legacy=14 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportScenarioSupport.h` | legacy=9 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportSchemaMapSupport.cpp` | legacy=31, fallback=18 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportSchemaMapSupport.h` | legacy=4 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportSidecarSettingsSupport.cpp` | legacy=20 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportSidecarSettingsSupport.h` | legacy=6 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportSourceResolver.cpp` | legacy=33, fallback=1 | active | One-time legacy DB import/migration path is intentional; P0/P3 pass replaced blocking archive waits with timer-driven process monitoring and retained safe temp cleanup guards. |
| `src/core/services/legacyimport/LegacyImportSourceResolver.h` | legacy=5 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportTypes.h` | legacy=13 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportUiSupport.cpp` | legacy=13 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportUiSupport.h` | legacy=7 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportVerifySupport.cpp` | legacy=42 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyImportVerifySupport.h` | legacy=5 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyOrderChildMapper.cpp` | legacy=4 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/LegacyOrderChildMapper.h` | legacy=2 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/MigrationPreflightValidator.cpp` | legacy=3 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/MigrationPreflightValidator.h` | legacy=4 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/MigrationPromoteCoordinator.cpp` | legacy=4 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/MigrationPromoteCoordinator.h` | legacy=5 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/MigrationRunJournal.cpp` | legacy=24 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/MigrationRunJournal.h` | legacy=16 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/MigrationStagingCoordinator.cpp` | legacy=8 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/MigrationStagingCoordinator.h` | legacy=6 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/MigrationVerifyCoordinator.cpp` | legacy=8 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/MigrationVerifyCoordinator.h` | legacy=5 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/RuntimeRecoveryCoordinator.cpp` | legacy=2 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/TargetConsistencyGate.cpp` | legacy=5 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/legacyimport/TargetConsistencyGate.h` | legacy=3 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/core/services/orders/CurrentOrdersInvoiceImportService.cpp` | fallback=2 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/orders/CurrentOrdersQueryService.cpp` | fallback=2 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/orders/OrderBatchMutationService.cpp` | fallback=2 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/orders/OrderFinancialService.cpp` | legacy=5, fallback=3 | active | Bounds exact-cent totals before conversion to historical numeric caches; preserves visible order-number lookup. |
| `src/core/services/orders/OrderFinancialService.h` | fallback=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/orders/OrderMutationFacade.cpp` | fallback=12 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/orders/OrderSyncImportService.cpp` | fallback=2 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/orders/OutsourceJobService.cpp` | legacy=4 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/orders/PricingRecalculationService.cpp` | legacy=15, fallback=4 | active | Pricing recalc keeps guarded compatibility handling for old pricing metadata and safe fallback policy resolution during settings-driven recalculation. |
| `src/core/services/orders/PricingRecalculationService.h` | legacy=2 | active | Pricing recalc exposes legacy repair APIs for controlled metadata and financial-sum normalization; guarded by build gates. |
| `src/core/services/orders/ServiceBatchMutationService.cpp` | fallback=2 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/orders/WorkPartsMutationService.cpp` | fallback=18 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/planning/PlanningRepository.cpp` | legacy=6 | active | Removes old date spelling in the same transaction before canonical upsert; keeps mutation audit. |
| `src/core/services/settings/AutoBackupSettingsService.cpp` | legacy=4 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/settings/GeneralSettingsService.cpp` | legacy=2 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/services/settings/GeneralSettingsService.h` | legacy=3 | active | Retains encrypted old API value for migration only; not sent to BuyerService. |
| `src/core/services/settings/UiSettingsService.cpp` | legacy=6 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/sync/SyncPayloadUtils.cpp` | fallback=2 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/util/SoundSupport.cpp` | legacy=5, fallback=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/core/util/VehicleNameRules.cpp` | legacy=3 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/db/sqlite/Db.h` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/db/sqlite/DbArchiveHydrationSupport.cpp` | legacy=1, fallback=7 | active | Archive hydration intentionally carries pricing metadata compatibility for old rows while preserving manual pricing state during restore. |
| `src/infrastructure/db/sqlite/DbArchiveInternals.h` | legacy=1 | active | Archive/canonical migration and compatibility declarations remain intentionally exposed for split archive routing units. |
| `src/infrastructure/db/sqlite/DbArchiveRouterCacheImportSupport.cpp` | legacy=20, fallback=4 | active | Copies discount_mode and markup_source from historical archives without changing their meaning. |
| `src/infrastructure/db/sqlite/DbArchiveRoutingLocalSupport.h` | fallback=3 | active | Archive/canonical migration and fallback handling are still part of persistence safety. |
| `src/infrastructure/db/sqlite/DbArchiveRoutingMemberFlow.cpp` | legacy=25, fallback=2 | active | Archive/canonical migration and fallback handling are still part of persistence safety. |
| `src/infrastructure/db/sqlite/DbArchiveRoutingStageSupport.cpp` | legacy=254, fallback=33 | active | Archive routing stage retains compatibility markers for old schema/stage data and must stay until archive migration is fully retired. |
| `src/infrastructure/db/sqlite/DbArchiveRoutingSupport.cpp` | legacy=37, fallback=7 | active | Adds explicit historical discount defaults to archive cache and active schema while retaining routing identity. |
| `src/infrastructure/db/sqlite/DbArchiveRoutingSupportModulePrelude.h` | legacy=6 | active | Archive/canonical migration and fallback handling are still part of persistence safety. |
| `src/infrastructure/db/sqlite/DbArchiveSourceSchemaSupport.cpp` | legacy=21, fallback=2 | active | Archive/canonical migration and fallback handling are still part of persistence safety. |
| `src/infrastructure/db/sqlite/DbArchiveSourceSchemaSupport.h` | legacy=2, fallback=1 | active | Archive/canonical migration and fallback handling are still part of persistence safety. |
| `src/infrastructure/db/sqlite/DbBootstrap.cpp` | legacy=7 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/db/sqlite/DbBootstrapEnsureSchema.cpp` | legacy=5, fallback=1 | active | Bootstrap schema repair intentionally handles old pricing metadata rows and financial-sum repair during startup. |
| `src/infrastructure/db/sqlite/DbBootstrapModulePrelude.h` | legacy=6 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/db/sqlite/DbCanonicalRewriteAncillaryImportSupport.cpp` | legacy=16, fallback=4 | active | Archive/canonical migration and fallback handling are still part of persistence safety. |
| `src/infrastructure/db/sqlite/DbCanonicalRewriteAncillaryImportSupport.h` | legacy=1 | active | Archive/canonical migration and fallback handling are still part of persistence safety. |
| `src/infrastructure/db/sqlite/DbCanonicalRewriteExecutionSupport.cpp` | legacy=3, fallback=3 | active | Archive/canonical migration and fallback handling are still part of persistence safety. |
| `src/infrastructure/db/sqlite/DbCanonicalRewriteFinalizeSupport.cpp` | legacy=4 | active | Archive/canonical migration and fallback handling are still part of persistence safety. |
| `src/infrastructure/db/sqlite/DbCanonicalRewritePrimaryImportSupport.cpp` | legacy=16, fallback=12 | active | Preserves historical discount_mode during canonical import; retains manual markup provenance. |
| `src/infrastructure/db/sqlite/DbCanonicalRewritePrimaryImportSupport.h` | legacy=1 | active | Archive/canonical migration and fallback handling are still part of persistence safety. |
| `src/infrastructure/db/sqlite/DbCatalogImportLocalSupport.h` | legacy=9, fallback=12 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/db/sqlite/DbContext.cpp` | fallback=8 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/db/sqlite/DbContext.h` | fallback=2 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/db/sqlite/DbDiagnostics.cpp` | legacy=12, fallback=8 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/db/sqlite/DbFacadeSupport.cpp` | legacy=1, fallback=7 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/db/sqlite/DbFacadeSupport.h` | legacy=3, fallback=4 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/db/sqlite/DbImportCatalogFromOrders.cpp` | legacy=18, fallback=7 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/db/sqlite/DbImportEnsureCatalogImportSchema.cpp` | legacy=6, fallback=2 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/db/sqlite/DbImportOrderStateLocalSupport.h` | fallback=3 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/db/sqlite/DbImportSupport.cpp` | legacy=26, fallback=2 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/db/sqlite/DbImportSupportModulePrelude.h` | legacy=6 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/db/sqlite/DbRuntimePreflight.cpp` | legacy=7, fallback=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/db/sqlite/DbSettingsRepository.cpp` | legacy=9, fallback=12 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/db/sqlite/DbSourceFingerprintSupport.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/db/sqlite/DbSourceFingerprintSupport.h` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/db/sqlite/DbWorkspaceOrderStateSupport.cpp` | legacy=1, fallback=3 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/db/sqlite/DbWorkspaceOrderStateSupport.h` | fallback=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/db/sqlite/LegacyCanonicalAncillaryImportSupport.cpp` | legacy=2, fallback=2 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/infrastructure/db/sqlite/LegacyCanonicalAncillaryImportSupport.h` | legacy=1 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/infrastructure/db/sqlite/LegacyCanonicalImportNormalization.cpp` | legacy=2 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/infrastructure/db/sqlite/LegacyCanonicalImportNormalization.h` | legacy=1 | active | One-time legacy DB import/migration path is an intentional supported feature. |
| `src/infrastructure/db/sqlite/OrderArchiveShardSqlSupport.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/db/sqlite/TransactionRunner.cpp` | fallback=7 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/db/sqlite/TransactionRunner.h` | fallback=2 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/security/EncryptedStorage.h` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/security/EncryptedStorageLocalSupport.h` | legacy=3 | active | Runtime compatibility declarations remain intentionally exposed for split encrypted-storage units. |
| `src/infrastructure/security/EncryptedStorageLocalSupport.cpp` | legacy=17 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/infrastructure/security/EncryptedStorageRuntime.cpp` | legacy=14, fallback=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/integrations/update/UpdateServiceSupport.cpp` | legacy=10 | active | Update handoff must preserve compatibility paths for existing legacy database locations during installer-driven upgrades. |
| `src/main.cpp` | fallback=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/runtime/AppRuntime.cpp` | legacy=6 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/runtime/AppRuntime.h` | legacy=3 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/licensing/LicenseAccessPolicy.cpp` | legacy=1 | active | Runtime license enforcement keeps one legacy compatibility branch for older license/session state while blocking data entry; remove only with licensing migration tests. |
| `src/licensing/LicenseInputBlockerFilter.cpp` | transitional=1 | active | Active license input blocker contains rollout/governance terminology and remains wired to the application event-filter path. |
| `src/licensing/LicenseInputBlockerFilter.h` | transitional=1 | active | Active license input blocker header is part of the rollout boundary and must stay inventoried for CI governance. |
| `src/licensing/LicenseUiLockController.h` | transitional=1 | active | Active license UI lock controller header is part of the data-entry blocking rollout and must stay inventoried for CI governance. |
| `src/ui/common/dialogs/MessageBoxSoundSupport.cpp` | fallback=6 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/common/dialogs/OrderDialog.cpp` | legacy=5, fallback=3 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/common/dialogs/OrderDialogBuildUi.cpp` | legacy=2 | active | Displays historical discount mode explicitly alongside new percent/uah choices. |
| `src/ui/common/dialogs/OrderDialogLifecycleFlow.cpp` | legacy=2 | active | Restores stored historical discount selection when reopening an order. |
| `src/ui/common/dialogs/OrderDialogModulePrelude.h` | legacy=5, fallback=3 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/common/dialogs/OrderDialogPdfFlow.cpp` | legacy=7 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/common/dialogs/ReceivedScansDialog.cpp` | fallback=4 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/common/dialogs/ServiceMastersDialog.cpp` | fallback=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/main/MainWindowBuildUi.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/main/MainWindowRuntime.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/catalog/CatalogPage.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/catalog/CatalogPageBuildUi.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/catalog/CatalogPageModulePrelude.h` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/catalog/CatalogPageRuntime.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/orders/CurrentOrdersPageActions.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/orders/CurrentOrdersPageBuildUi.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/orders/CurrentOrdersPageLocalSupport.cpp` | legacy=1, fallback=3 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/orders/PartTableController.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/orders/ServiceTableController.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/orders/WorkPartsAssignmentCoordinator.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/orders/WorkPartsAttachmentCoordinator.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/orders/WorkPartsCrudController.cpp` | legacy=1, fallback=4 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/orders/WorkPartsDialog.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/orders/WorkPartsDialogModulePrelude.h` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/orders/WorkPartsDialogSupport.cpp` | legacy=1, fallback=2 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/orders/WorkPartsDialogUiBuilder.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/orders/WorkPartsEditorController.cpp` | legacy=1, fallback=6 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/orders/WorkPartsImportCoordinator.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/orders/WorkPartsInlineEditorController.h` | legacy=1, fallback=3 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/orders/WorkPartsNavigationController.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/orders/WorkPartsOutsourceSupport.cpp` | legacy=2, fallback=20 | active | Documents old return_date as planned date, not evidence of actual receipt; existing service lookup retained. |
| `src/ui/tabs/orders/WorkPartsPartEditServices.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/orders/WorkPartsPartPersistence.cpp` | legacy=1, fallback=3 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/orders/WorkPartsPersistenceCoordinator.cpp` | legacy=1, fallback=3 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/orders/WorkPartsPricingSupport.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/orders/WorkPartsServicePersistence.cpp` | legacy=1, fallback=6 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/orders/WorkPartsTableController.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/parts/PartsPageBuildUi.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/parts/PartsPageRuntime.cpp` | legacy=1, fallback=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/reports/ReportsPageBuildUi.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/reports/ReportsPageExportFlow.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/reports/ReportsPageRefreshFlow.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/schedule/PlanningPageBuildUi.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/schedule/PlanningPageSupport.cpp` | fallback=3 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/schedule/PlanningPageSupport.h` | fallback=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/schedule/PlanningPopupController.cpp` | fallback=3 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/schedule/PlanningReminderController.cpp` | fallback=2 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/settings/AutoBackupSettingsSection.cpp` | fallback=4 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/settings/SettingsFinanceSection.cpp` | fallback=7 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/settings/SettingsGoogleSheetsSectionSupport.cpp` | legacy=14 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/settings/SettingsPage.cpp` | legacy=3 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/settings/SettingsPage.h` | legacy=2 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/settings/SettingsPageBuildFlow.cpp` | legacy=12 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/settings/SettingsPageLocalSupport.cpp` | legacy=16 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/settings/SettingsPageModulePrelude.h` | legacy=3 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/settings/SettingsPageNavigationFlow.cpp` | legacy=2 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/settings/SettingsSyncSectionSupportBindings.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/settings/SettingsSyncSectionSupportBindingsActions.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/settings/SettingsSyncSectionSupportBindingsState.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/settings/SettingsSyncSectionSupportBuildBluetooth.cpp` | legacy=2 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/settings/SettingsSyncSectionSupportBuildLayout.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `src/ui/tabs/settings/SettingsSyncSectionSupportBuildWifi.cpp` | legacy=1 | active | Runtime fallback/compatibility behavior is wired and must not be removed without call-graph and compile checks. |
| `tests/InvoicePdfRecognitionTests.cpp` | fallback=2 | test-only | Regression/smoke/test reference; not runtime product code. |
| `tests/LegacyCanonicalAncillaryImportSupportTests.cpp` | legacy=7 | test-only | Regression/smoke/test reference; not runtime product code. |
| `tests/regression/RegressionP0DbContextTransactionTests.cpp` | fallback=1, legacy=2 | test-only | Regression/smoke/test reference; not runtime product code. |
| `tests/regression/RegressionP0ImportIncrementalProcessIoTests.cpp` | fallback=6 | test-only | Regression/smoke/test reference; not runtime product code. |
| `tests/regression/RegressionP0ImportModuleExtractionTests.cpp` | legacy=3 | test-only | Regression/smoke/test reference; not runtime product code. |
| `tests/regression/RegressionP0ImportNoUiWaitTests.cpp` | fallback=4 | test-only | Regression/smoke/test reference; not runtime product code. |
| `tests/regression/RegressionP0ImportProcessBoundaryTests.cpp` | fallback=3 | test-only | Regression/smoke/test reference; not runtime product code. |
| `tests/regression/RegressionP0InvoiceImportScenarioTests.cpp` | fallback=4, legacy=1 | test-only | Regression/smoke/test reference; not runtime product code. |
| `tests/regression/RegressionP0SQLiteIntegrationTests.cpp` | legacy=3 | test-only | Regression/smoke/test reference; not runtime product code. |
| `tests/regression/RegressionP1InlineEditorControllerSplitTests.cpp` | legacy=4 | test-only | Regression/smoke/test reference; not runtime product code. |
| `tests/regression/RegressionP1PartsPageIncrementalRefreshTests.cpp` | fallback=1 | test-only | Regression/smoke/test reference; not runtime product code. |
| `tests/regression/RegressionP2ArchitectureIncModuleTests.cpp` | legacy=5 | test-only | Regression/smoke/test reference; not runtime product code. |
| `tests/regression/RegressionP2ArchitectureModuleBoundaryTests.cpp` | legacy=1 | test-only | Regression/smoke/test reference; not runtime product code. |
| `tests/regression/RegressionLicenseAccessPolicyTests.cpp` | legacy=1 | test-only | Licensing regression coverage validates compatibility and blocking behavior; marker usage is test-only and not runtime product code. |
| `tests/regression/RegressionP2LegacyFallbackInventoryTests.cpp` | legacy: controlled, fallback: controlled, MissingMethods: controlled, transitional: controlled, allowlist: controlled | test-only | Regression/smoke/test reference; not runtime product code. |
| `tests/unit/P2AcceptanceUnitTests.cpp` | fallback=1 | test-only | P2 acceptance unit coverage reference; not runtime product code. |
| `tests/regression/RegressionMarkupRecalculationTests.cpp` | legacy=5, fallback=5 | test-only | Regression tests intentionally assert pricing legacy/fallback guards and do not ship as runtime logic. |

| `src/core/services/settings/SyncSettingsService.cpp` | fallback=1 | active | Production/build controlled marker added or retained; requires ADR-tracked explanation before release. |

| `docs/AUDIT_UPDATE_TZ_EXECUTION_REPORT.md` | transitional=1, allowlist=1 | active | Audit report records update stabilisation checks; tracked so marker inventory gate remains deterministic. |
| `docs/AUDIT_TZ_PRICING_V2_2.txt` | legacy=3 | active | Audit report records pricing v2.2 schema compatibility findings; documentation-only marker inventory entry, not runtime logic. |
| `docs/AUDIT_TZ_PRICING_V2_2_FINAL_REAUDIT_100.txt` | legacy=6, fallback=4 | active | Final re-audit report records resolved pricing v2.2 compatibility and fallback findings; documentation-only marker inventory entry, not runtime logic. |


| `src/runtime/RuntimeJobDispatcher.cpp` | fallback=1 | active | Runtime dispatcher contains a compatibility wording marker in telemetry/cancellation notes; tracked to keep controlled-marker CI deterministic. |
| `src/ui/common/TabularReadModelMapper.cpp` | legacy=1 | active | Mapper is intentionally snapshot-only while retaining compatibility terminology in comments/tests; tracked so old item-model paths cannot silently return. |
| `src/ui/common/TabularReadModelMapper.h` | legacy=2 | active | Header documents removed legacy item-model APIs and snapshot-only contract; tracked by ADR to prevent accidental marker-budget growth. |
| `src/ui/tabs/orders/PartTableController.cpp` | legacy=1, fallback=1 | active | Work-parts UI controller retains compatibility wording around attachment/invoice metadata refresh while batch rebuild paths are guarded. |
| `src/ui/tabs/orders/ServiceTableController.cpp` | legacy=1, fallback=1 | active | Service table controller retains compatibility wording around attachment refresh while batch rebuild paths are guarded. |
| `src/ui/tabs/orders/WorkPartsDialog.cpp` | legacy=2 | active | WorkParts dialog still contains tracked compatibility terminology for the guarded restore/async migration path; no new sync UI path is allowed. |

| `docs/AUDIT_PERFORMANCE_TZ_CONTINUATION_100_PASS_2_2026_05_15.md` | legacy=1 | active | Performance execution report mentions historical table-model compatibility; documentation-only marker inventory entry, not runtime logic. |
| `docs/AUDIT_PERFORMANCE_TZ_CONTINUATION_100_PASS_3_2026_05_15.md` | legacy=1 | active | Performance execution report mentions historical table-model compatibility; documentation-only marker inventory entry, not runtime logic. |
| `docs/AUDIT_PERFORMANCE_TZ_CONTINUATION_100_PASS_4_2026_05_15.md` | legacy=1, fallback=1 | active | Performance execution report records controlled compatibility/fallback closure notes; documentation-only marker inventory entry. |
| `docs/AUDIT_PERFORMANCE_TZ_CONTINUATION_100_PASS_5_2026_05_15.md` | legacy=1 | active | Performance execution report mentions historical table-model compatibility; documentation-only marker inventory entry, not runtime logic. |
| `docs/AUDIT_PERFORMANCE_TZ_PASS7_FREEZE_LOG_DB_2026_05_15.md` | legacy=2 | active | Performance execution report records tracked compatibility terminology for freeze/log/DB closure; documentation-only marker inventory entry. |
| `docs/AUDIT_PERFORMANCE_TZ_PASS9_SNAPSHOT_CACHE_GATES_2026_05_15.md` | legacy=1 | active | Performance execution report records snapshot migration compatibility notes; documentation-only marker inventory entry. |
| `docs/AUDIT_PERFORMANCE_TZ_PASS12_WORKPARTS_ROW_INDEX_2026_05_15.md` | legacy=2 | active | Performance execution report records WorkParts row-index compatibility notes; documentation-only marker inventory entry. |
| `docs/AUDIT_PASS15_MSVC_COMPILE_FIX_2026_05_15.md` | legacy=2 | active | Build-fix report references legacy-tail gate script names; documentation-only marker inventory entry, not runtime logic. |

| `tests/regression/RegressionPerformanceTzPass4WorkPartsAsyncReadTests.cpp` | legacy=1, fallback=3 | test-only | Regression test asserts guarded WorkParts async-read migration terms; marker usage is test-only inventory coverage. |
| `tests/regression/RegressionPerformanceTzPass7FreezeLogDbClosureTests.cpp` | fallback=3 | test-only | Regression test asserts fallback/logging closure guard behavior; marker usage is test-only inventory coverage. |
| `tests/regression/RegressionPerformanceTzPass9SnapshotCacheGatesTests.cpp` | legacy=1 | test-only | Regression test asserts snapshot migration guard behavior; marker usage is test-only inventory coverage. |
| `tests/regression/RegressionPerformanceTzPass12WorkPartsRowIndexTests.cpp` | legacy=2 | test-only | Regression test asserts WorkParts row-index migration guard behavior; marker usage is test-only inventory coverage. |

## Follow-up rules
1. New `legacy/fallback/MissingMethods/transitional/allowlist` entries must either be added to this inventory or removed before release.
2. `deprecated but used` entries may be removed only after compile-check, grep/call-graph verification, and targeted regression tests.
3. Runtime fallback paths may not be removed in the same pass as inventory work, because they are often safety mechanisms for import, backup, update, and archive recovery.

## P2 p3–p4 follow-up status
- Cleanup/deletion is now guarded by `scripts/check_legacy_tail_cleanup.py`.
- No runtime deletion was performed without compile-check proof.
- Grep/call-graph verification is recorded in ADR-0003.
- Old synchronous fallback paths must carry `CS_INTERNAL_BLOCKING_SYNC_ALLOWED` or `CS_INTERNAL_SYNC_FALLBACK_INTERNAL_ONLY`.

## P2 p5–p6 follow-up status
- Transitional runtime allowlist budget is enforced by `scripts/check_legacy_marker_ci_gate.py`.
- Runtime transitional allowlist reduced from 3 entries to 1 entry.
- New production `legacy/fallback/MissingMethods` markers require an ADR-0002 explanation and may not exceed recorded marker counts.
- CI workflow added at `.github/workflows/code-quality-gates.yml`.
- Regression coverage added/retained: `tests/regression/RegressionP2LegacyTailCleanupGateTests.cpp`, `tests/regression/RegressionP2LegacyTailCiGateTests.cpp`.

| `docs/AUDIT_TZ_P0_STABILIZATION_2026_05_13.txt` | legacy=7, allowlist=1 | active | Execution report for P0 stabilization pass; contains controlled references to touched legacy resolver and safety allowlist policy. |
| `scripts/check_p0_safety_gate.py` | legacy=2, allowlist=1 | active | Governance/build gate script for P0 safety checks; references guarded legacy source path allowlist only as static policy input. |
| `scripts/check_monolith_split_gate.py` | legacy=1 | active | Governance/build gate script for P1/P2 monolith split checks; marker appears only in required fragment filename and is not runtime legacy logic. |

| `docs/AUDIT_PERFORMANCE_TZ_EXECUTION_2026_05_14.txt` | fallback=3 | active | Performance audit report records invoice import blocking/fallback findings and execution notes; documentation-only marker inventory entry, not runtime logic. |

| `docs/AUDIT_LOG_CODE_PERFORMANCE_FIX_2026_06_04.md` | legacy=1 | active | Execution report added during CRM transfer validation; documentation-only marker inventory entry. |
| `docs/SOUND_SYSTEM_AUDIT_FIX_2026_06_04.md` | legacy=2, fallback=2 | active | Sound-system execution report records compatibility wording for settings/resource loading; documentation-only marker inventory entry. |
| `docs/CRM_TRANSFER_GATE_FIX_2026_06_04.md` | legacy=1, transitional=1 | active | CRM transfer gate-fix report records controlled inventory and architecture-debt terminology; documentation-only marker inventory entry. |
| `docs/CRM_TRANSFER_TO_CS_PG_V2_1_1_2026_06_04.md` | legacy=1, transitional=1, allowlist=1 | active | CRM transfer report records controlled gate policy and inventory terms; documentation-only marker inventory entry. |
| `docs/GLOBAL_PERFORMANCE_STABILITY_AUDIT_TZ_2026_06_06.md` | legacy=1, fallback=10 | active | Stabilization TZ/audit document records controlled performance and compatibility findings; documentation-only marker inventory entry. |
| `docs/GLOBAL_STABILIZATION_100_IMPLEMENTATION_2026_06_06.md` | legacy=2, fallback=2 | active | Stabilization implementation report records controlled compatibility and inventory-gate terminology; documentation-only marker inventory entry. |
| `docs/VEHICLE_INTAKE_FINAL_LAYOUT_AND_PDF_MATCH_2026_06_06.md` | fallback=1 | active | Vehicle-intake PDF audit report records a controlled data-source fallback note; documentation-only marker inventory entry. |
| `docs/CRM_CLIENT_CARD_VISUAL_FIX_2026_06_06.md` | legacy=1 | active | CRM visual-fix report records the legacy-tail inventory gate name from the blocked build; documentation-only marker inventory entry. |
| `docs/PDF_TEXT_AND_BLANK_ACT_POPUP_FIX_2026_06_06.md` | fallback=1 | active | PDF/blank-act report records controlled branding fallback wording; documentation-only marker inventory entry. |
| `src/core/cloudsync/CloudSyncClient.cpp` | legacy=1, fallback=3 | active | Calls retireLegacyMetadata before outbox synchronization; retains malformed-event identity for diagnostics. |
| `src/core/cloudsync/CloudSyncSchemaService.cpp` | legacy=1 | active | Cloud sync schema bootstrap retains compatibility terminology for migrated sync tables; active CRM/cloudsync runtime source. |
| `src/core/cloudsync/ConflictResolverService.cpp` | fallback=2 | active | Conflict resolver contains migrated sync compatibility terminology; active CRM/cloudsync runtime source. |
| `src/core/cloudsync/DeviceIdentityService.cpp` | legacy=3 | active | Device identity service retains compatibility terminology for migrated desktop identity settings; active CRM/cloudsync runtime source. |
| `src/core/cloudsync/EntityUidService.cpp` | legacy=1 | active | Entity UID helper retains compatibility terminology for migrated CRM/cloudsync identifiers; active runtime source. |
| `src/core/cloudsync/RemoteMutationApplyService.cpp` | fallback=2 | active | Remote mutation apply service retains compatibility terminology for cloudsync payload migration; active runtime source. |
| `src/core/crm/CrmClientService.h` | legacy=1 | active | CRM client service exposes active migrated CRM API; terminology is tracked for transfer gates. |
| `src/core/crm/CrmTaskService.cpp` | fallback=4 | active | CRM task service is active migrated runtime source with controlled compatibility terminology. |
| `src/core/crm/CrmTimelineService.cpp` | fallback=2 | active | CRM timeline service is active migrated runtime source with controlled compatibility terminology. |
| `src/core/crm/CrmVehicleService.h` | legacy=1 | active | CRM vehicle service exposes active migrated CRM API; terminology is tracked for transfer gates. |
| `src/core/crm/OnlineBookingService.cpp` | fallback=8 | active | Online booking service is active CRM runtime source; fallback wording is tied to guarded mutation error handling. |
| `src/core/crm/VehicleIntakeService.cpp` | fallback=2 | active | Vehicle intake service is active migrated CRM runtime source with controlled compatibility terminology. |
| `src/infrastructure/db/sqlite/DbThreadConnectionSupport.cpp` | legacy=1 | active | SQLite thread connection support is active infrastructure for scoped DB handles; fallback terminology remains controlled. |
| `web-crm/scripts/check_static_readiness.py` | legacy=1 | active | Web CRM static readiness gate belongs to the transferred CRM source tree and is intentionally retained. |

| `docs/development-baseline-check_legacy_tail_inventory.json` | legacy:controlled, fallback:controlled | test-only | Exact diagnostic baseline for opt-in remediation library; original release gate remains mandatory, no whole-source exclusion. |

| `docs/development-baseline-check_legacy_marker_ci_gate.json` | legacy:controlled, fallback:controlled | test-only | Exact diagnostic baseline for opt-in remediation library; original release gate remains mandatory, no whole-source exclusion. |

| `docs/development-baseline-check_project_layout.json` | legacy:controlled, fallback:controlled | test-only | Exact diagnostic baseline for opt-in remediation library; original release gate remains mandatory, no whole-source exclusion. |

| `docs/development-baseline-check_perf_guards.json` | legacy:controlled, fallback:controlled | test-only | Exact diagnostic baseline for opt-in remediation library; original release gate remains mandatory, no whole-source exclusion. |

| `src/ui/tabs/statistics/StatsPage.cpp` | legacy=6, fallback=3 | active | Historical audit labels, exclusion of synthetic legacy actors, and display-only fallback values; no alternate persistence path. |

| `src/core/services/statistics/StatisticsReadService.cpp` | legacy=3 | active | Preserves includeLegacy=false and serialization of the existing audit legacy flag extracted from StatsPage; no new compatibility branch. |

## Compatibility inventory review 2026-10-30

Counts below classify concrete existing compatibility behavior, not architecture exemptions. Architecture baseline remains empty. New occurrences still fail the numeric gate.

| File | Markers | Status | Review |
|---|---|---|---|
| `src/core/cloudsync/CloudSyncOutboxService.cpp` | legacy=4 | active | Retires old metadata and reconciles confirmed old mutation-log deletions; truncated snapshots cannot imply deletion. |
| `src/core/cloudsync/CloudSyncOutboxService.h` | legacy=1 | active | Declares the metadata retirement operation used by synchronization startup. |
| `src/core/finance/ExactMoney.h` | legacy=1 | active | Documents the explicit adapter requirement for historical REAL money values. |
| `src/core/repository/PartRepository.cpp` | legacy=1 | active | Selects historical save pricing for existing quotes to avoid repricing old purchases. |
| `src/core/services/AccessControlService.cpp` | legacy=2 | active | Retains number-based scope for records without UID and handles historical navigation permissions. |
| `src/core/services/AccessControlService.h` | legacy=2 | active | Uses historical order numbers only when UID is absent; restricted access remains enforced. |
| `src/core/services/ActivityAuditService.cpp` | legacy=21 | active | Reads the persisted legacy_mutation_log with date and limit filters; does not fabricate modern actors. |
| `src/core/services/ActivityAuditService.h` | legacy=2 | active | Exposes explicit historical-event and include-history flags in audit results and filters. |
| `src/core/services/InvoiceEvidenceService.cpp` | legacy=1 | active | Comment explains SQLite variable-count batching; no additional compatibility execution path. |
| `src/core/services/InvoiceRegistryService.cpp` | legacy=3, fallback=2 | active | Uses document identity when metadata is absent and preserves old issued-part debt allocation. |
| `src/core/services/orders/OrderAttachmentService.cpp` | legacy=3 | active | Avoids deleting attachments still referenced by historical archive absolute paths. |
| `src/core/services/orders/OrderDialogReadQueryService.cpp` | legacy=1 | active | Reads historical discount_mode explicitly instead of reinterpreting old discounts as percent. |
| `src/core/services/orders/OrderDialogStateSignature.h` | legacy=1 | active | Historical discount mode participates in concurrency state signatures. |
| `src/core/services/orders/OrderSnapshotService.cpp` | legacy=1 | active | Reads historical discount semantics into the order snapshot. |
| `src/core/services/orders/OrderSnapshotService.h` | legacy=1 | active | Carries historical discount mode in the snapshot contract. |
| `src/core/services/orders/PartPricingEngine.cpp` | legacy=6 | active | Preserves existing quoted prices for report/recalc/old-save sources; new and inventory quotes use markup calculation. |
| `src/core/services/OrderWorkspaceService.h` | legacy=1 | active | Preserves historical discount mode in workspace save input. |
| `src/core/services/PurchasePaymentService.cpp` | legacy=10 | active | Reconciles historical stock payments under a savepoint, rolling back failed allocation. |
| `src/core/services/StaffAccountService.cpp` | legacy=2 | active | Verifies previously stored SHA-256 PIN hashes without displaying or inventing credentials. |
| `src/core/services/StatisticsSnapshotService.cpp` | legacy=2 | active | Reads explicit historical discount mode in statistics order queries. |
| `src/core/services/storage/StorageDirectoriesGuard.cpp` | fallback=1 | active | Disables QSaveFile direct writes to avoid partial destination publication. |
| `src/core/services/SupplierInvoiceLedger.cpp` | fallback=1 | active | Documents exact supplier-code matching; prohibits guessing aliases or stripping brand prefixes. |
| `src/core/services/WorkshopInventoryService.h` | legacy=1 | active | Unset client price preserves historical purchase pricing. |
| `src/core/util/FormatUtils.cpp` | legacy=1 | active | Documents historical discount amounts to prevent unit reinterpretation. |
| `src/ui/common/dialogs/InvoiceDocumentOpenSupport.h` | legacy=1 | active | Documents read-only viewing of old invoice attachments without replacing original sources. |
| `src/ui/common/dialogs/OrderClosureFlow.cpp` | legacy=1 | active | Uses protected-token reading policy shared with synchronization, avoiding plaintext migration on closure. |
| `src/ui/tabs/inventory/WorkshopInventoryPage.cpp` | fallback=7 | active | Provides missing-cell placeholders, bounded default column sizes and delete handling when application filter is absent. |
| `src/ui/tabs/orders/BayOccupancyPage.cpp` | legacy=1 | active | Reads protected credentials without migration writes from settings load. |
| `src/ui/tabs/orders/WorkPartsRepairAdvisorFlow.cpp` | legacy=1, fallback=2 | active | Extracted implementation retains transaction error messages for repair-advisor persistence. |
| `src/ui/tabs/reports/PurchaseDebtsPage.cpp` | legacy=1 | active | Requires evidence for historical dates instead of silently changing their year. |

### Supporting evidence and server compatibility review

| File | Markers | Status | Review |
|---|---|---|---|
| `docs/CURRENT_BUILD.md` | legacy=3, fallback=2 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/ELIT_STAGE12_AUDIT.md` | allowlist=1 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/ELIT_STAGE13_PDF.md` | allowlist=2 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/ELIT_STAGE14_PDF_PREPARATION.md` | legacy=1 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/ELIT_SYNC_PROGRESS.md` | legacy=6, fallback=2 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/FINANCIAL_FIXES_2026_10_28.md` | legacy=1 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/POINTS_4_5_6_2026_10_26.md` | legacy=1 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/REPORTS_SUPPLIERS_2026_10_23.md` | fallback=1 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/WORKFLOW_FIXES_2026_10_23.md` | legacy=2 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/date-format-migration-20260930.txt` | legacy=1, fallback=1 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/mobile-2.9.0-verification.md` | legacy=2 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/report-header-five-concepts-handoff.md` | fallback=1 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/audit-remediation/evidence/PurchasePaymentTests.txt` | legacy=307 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/audit-stage1-2026-09-30/evidence/BackupRecoveryAcceptanceTests.txt` | legacy=1 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/audit-stage1-2026-09-30/evidence/PurchasePaymentTests.txt` | legacy=309 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/debt-windows-performance-20261026/test-baseline.txt` | legacy=1 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/debt-windows-performance-20261026/test-scaled5.txt` | legacy=1 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/financial-audit-20261028/README.md` | legacy=1 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/financial-audit-20261028/test-results.md` | legacy=5 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/performance-audit-20261026/dataset-manifest.json` | legacy=2 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/performance-audit-20261026/sql-profile-baseline.json` | legacy=2 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/performance-audit-20261026/sql-profile-scaled5.json` | legacy=2 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/performance-audit-20261026/test-baseline.txt` | legacy=1 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/performance-audit-20261026/test-scaled5.txt` | legacy=1 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/search-audit-2026-09-30/OrderSearchPerformanceTests.txt` | legacy=1 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/stage2-2026-09-30/CONTRACT.md` | legacy=5 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/stage2-2026-09-30/README.md` | legacy=1 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/stage2-2026-09-30/evidence/server-postgres.txt` | legacy=4 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/stage2-2026-09-30/evidence/server-sqlite.txt` | legacy=4 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/stage3-2026-09-30/evidence/BackupRecoveryAcceptanceTests.txt` | legacy=1 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/stage3-2026-09-30/evidence/server-postgresql.txt` | legacy=2 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `docs/stage3-2026-09-30/evidence/server-sqlite.txt` | legacy=6 | active | Historical audit/report evidence retained for traceability; marker text is descriptive and does not execute runtime code. |
| `scripts/NativeDatabaseMaintenance.cpp` | legacy=1 | active | Maintenance entry invokes metadata retirement through CloudSyncOutboxService. |
| `scripts/desktop_authority_plan.py` | legacy=1 | active | Produces an explicit plan of covered historical event identifiers. |
| `scripts/desktop_authority_server.py` | legacy=1 | active | Applies only historical event identifiers enumerated by the reconciliation plan. |
| `scripts/patch_discount_consumers.py` | legacy=9 | active | One-off source migration utility retained as evidence; not a runtime or build step. |
| `scripts/patch_discount_mode.py` | legacy=5 | active | One-off source migration utility retained as evidence; not a runtime or build step. |
| `scripts/patch_discount_routing.py` | legacy=10 | active | One-off source migration utility retained as evidence; not a runtime or build step. |
| `scripts/repair_server_sync_operation.py` | legacy=1 | active | Explicit repair utility for historical canonical rewrite operation identifiers. |
| `scripts/run_development_gate.py` | legacy=2 | active | Development-only diagnostic wrapper; full delivery requires remediation OFF and strict gates. |
| `scripts/run_financial_audit.py` | legacy=1 | active | Invokes historical partial-payment regression scenarios on isolated audit data. |
| `scripts/summarize_performance_audit.py` | legacy=1 | active | Checks the recorded historical archive fixture checksum in audit output. |
| `scripts/test_architecture_ratchet.py` | allowlist=1 | active | Negative controls verify previously exempt source files remain scanned. |
| `scripts/test_stage3_recovery.ps1` | legacy=2 | active | Isolated recovery tests account for sealed and unsealed historical database paths. |
| `server/alembic/versions/0014_bay_occupancy.py` | legacy=1 | active | Migration creates actual bay history without inventing history from old post fields. |
| `server/app/api/v1/client_cabinet.py` | legacy=1 | active | Historical attachments without explicit visibility are not exposed to clients. |
| `server/app/api/v1/mobile.py` | legacy=1, fallback=2 | active | Preserves literal historical make/model and supplies missing serialized fields. |
| `server/app/services/bay_occupancy_service.py` | legacy=1 | active | Documents unknown historical occupancy; leaves finance and order fields unchanged. |
| `server/app/services/booking_availability.py` | legacy=1 | active | Treats unassigned historical resource intervals conservatively as exclusive. |
| `server/app/services/order_closure_service.py` | legacy=1 | active | Preserves explicit historical discount mode in closure basis. |
| `server/app/services/planning_sync_service.py` | legacy=2 | active | Resolves old master names only when unambiguous and uses the same ownership rules as calendar. |
| `server/app/services/repair_ai.py` | fallback=6 | active | Combines deterministic repair rules with online, learned and historical suggestions. |
| `server/app/services/sync_access.py` | legacy=2 | active | Documents single-station historical scope; empty scope is not cross-tenant authorization. |
| `server/app/services/sync_service.py` | legacy=1 | active | Keeps ACK conflict policy for clients without revision metadata. |
| `server/scripts/reconcile_desktop_snapshot.py` | legacy=1 | active | Reads confirmed historical order deletion audit identifiers for reconciliation. |
| `server/tests/test_order_closure.py` | legacy=1 | test-only | Isolated regression coverage for historical data and compatibility behavior; no production exemption. |
| `server/tests/test_stage2_sync.py` | legacy=2 | test-only | Isolated regression coverage for historical data and compatibility behavior; no production exemption. |
| `server/tests/test_stage3_security.py` | legacy=3 | test-only | Isolated regression coverage for historical data and compatibility behavior; no production exemption. |
| `tests/BayLoadReportTests.cpp` | legacy=1 | test-only | Isolated regression coverage for historical data and compatibility behavior; no production exemption. |
| `tests/DataAttachmentSafetyTests.cpp` | legacy=4 | test-only | Isolated regression coverage for historical data and compatibility behavior; no production exemption. |
| `tests/ElitInvoiceStorageTests.cpp` | legacy=2, fallback=1 | test-only | Isolated regression coverage for historical data and compatibility behavior; no production exemption. |
| `tests/HistoryPartsAutofillTests.cpp` | legacy=1 | test-only | Isolated regression coverage for historical data and compatibility behavior; no production exemption. |
| `tests/LocalWorkshopBackendTests.cpp` | legacy=1 | test-only | Isolated regression coverage for historical data and compatibility behavior; no production exemption. |
| `tests/MobileSyncTests.cpp` | legacy=8 | test-only | Isolated regression coverage for historical data and compatibility behavior; no production exemption. |
| `tests/OrderTableUiMapperTests.cpp` | legacy=2 | test-only | Isolated regression coverage for historical data and compatibility behavior; no production exemption. |
| `tests/PurchasePaymentTests.cpp` | legacy=38, fallback=9 | test-only | Isolated regression coverage for historical data and compatibility behavior; no production exemption. |
| `tests/ReportsOrdersTests.cpp` | legacy=2 | test-only | Isolated regression coverage for historical data and compatibility behavior; no production exemption. |
| `tests/regression/RegressionActivityAuditServiceTests.cpp` | legacy=16 | test-only | Isolated regression coverage for historical data and compatibility behavior; no production exemption. |
| `tests/regression/RegressionWorkshopInventoryServiceTests.cpp` | legacy=1 | test-only | Isolated regression coverage for historical data and compatibility behavior; no production exemption. |
| `scripts/test_project_source_scope.py` | legacy=controlled | test-only | Negative controls reject unlisted source markers and numeric growth after inventory review. |

| `scripts/run_change_gates.py` | legacy=controlled | active | Invokes the existing marker CI gate in static acceptance; no runtime exemption or marker budget expansion. |

## DB-YEAR-212 evidence documentation

| File | Status | Rationale |
|---|---|---|
| `docs/reliability/DB_YEAR_212.md` | active | Migration plan references existing legacy import paths; documentation only, no runtime exception. |
| `docs/reliability/STATUS.md` | active | Persistent evidence and unresolved migration defects; documentation only, no runtime exception. |
| `docs/reliability/PLAN.md` | active | Persistent reliability milestone references historical schema migration tests; documentation only, no runtime exception. |

| `tests/MigrationChildKeyProjectionTests.cpp` | test-only | Attaches a synthetic SQLite source as legacy_rewrite to exercise the actual canonical import query and child-key mapping. |
| `docs/reliability/DBY_T163_CLOUD.md` | active | Evidence names LegacyOrderChildMapper exercised by the synthetic child-key test. |
| `SOURCE_SHA256.json` | active | The public source checksum inventory lists migration filenames containing legacy markers. It contains paths and digests only. |
| `tests/MigrationRepeatJournalTests.cpp` | test-only | Exercises LegacyImportJournal on an isolated duplicate import. |
| `docs/reliability/DBY_T166_REPEAT.md` | active | Records commands and expected behavior for repeated legacy import. |
| `tests/MigrationRetainedStoresTests.cpp` | test-only | Exercises preservation of previous encrypted archive, registry and legacy store files in an isolated fixture. |
| `docs/reliability/DBY_T171_PUBLICATION.md` | active | Records the migration-service rollback marker and the crash/retry fixtures used for publication validation. |
| `docs/reliability/DBY_T172_REGISTRY_KEYS.md` | active | Records key reservation for archived orders during migration. |
