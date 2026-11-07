# DBY-T218 — registered annual UI/PDF regression

Classification: development verification and Windows acceptance handoff. Continues DBY-07/T217; not a completed native acceptance claim. Last numeric quota supplied was 33%, stop 25%. Fresh reading requested before a broad run; no broad build, native run or performance run started here.

## Production path and change

Registered `ReportsOrdersTests::annualOrderUiPdfVisibleIdentity` uses real OrderDialog objects and actual PDF painting. `renderPdfPreview` is the shared path used by the UI preview action and this slot; it checks current order permission and performs the export. Opening an external viewer is a separate subsequent action. OrderDialogPdfService retains default open-after-export behavior for other callers; the shared renderer requests export only, so automation does not launch Edge/Acrobat.

Before-fix commit **d2f57dd** adds the registered slot and export/open separation, retaining the internal-number header and filename. It is a reproducible Windows comparison revision, not a measured RED result. The following correction uses the form's loaded public number in exactly two presentation expressions: preview filename input and PDF «№ наряду». Permissions, financial reads, canonical UID detail loading and archive routing continue using internal/UID identities. No global replacement of internal numbers.

## Slot and retained artifacts

Two synthetic orders: internal 900101/900202, stable UID year-2025/year-2026, years 2025/2026, public number 7; distinct cars and part markers. For each the slot:

1. Opens the real form and verifies its recoveryOrderUid, saves a screenshot.
2. Renders both customer and parts PDFs through the same preview path as the button; verifies the actual filename and extracts text with real pdftotext. Checks visible number 7 and inclusion/exclusion of each unique part marker.
3. Archives the 2025 order via Database::archiveSingleActiveOrderToArchive, closes and prepares/opens the profile again.
4. Opens HistoryPage, types search 7, waits for both UID rows, saves the search screenshot, invokes the table's ordinary doubleClicked signal and checks the opened OrderDialog's UID for each row. A watchdog prevents a routing-error modal from hanging the test.

PDF, PNG and extracted TXT remain in **CS_ORDER_YEAR_PDF_OUTPUT**, or `annual-order-pdf-evidence` next to the test executable if unset. Use different output directories for before and after. The SQLite profile is a separate QTemporaryDir with CS_TEST_DATA_ROOT restored on exit. No private profile is used. Filename/header failures are collected so both orders' output is retained before the final assertion. Native environment/setup/routing failures still stop immediately with their real diagnostic.

Expected after-fix PDF filenames:
- `7_03.02.2025.pdf`, `7_03.02.2025_parts_report.pdf`
- `7_03.02.2026.pdf`, `7_03.02.2026_parts_report.pdf`

## Windows invocation

Build the ordinary ReportsOrdersTests target under the existing gate configuration, then in PowerShell:
```powershell
$env:CS_ORDER_YEAR_PDF_OUTPUT = "$PWD/build-single-instance-audit/year-pdf-after"
# Optional if the standard bundled Poppler path differs:
# $env:CS_TEST_PDFTOTEXT = 'C:\path\to\pdftotext.exe'
& .\build-single-instance-audit\tests\ReportsOrdersTests.exe annualOrderUiPdfVisibleIdentity -o .\build-single-instance-audit\year-pdf-after.txt,txt
```

For before-fix evidence use d2f57dd in a separate checkout/build (or the desktop's controlled before build) and `year-pdf-before` output. Same slot and expectations in both revisions. Missing pdftotext is a failure, never a skip. Related existing slot: `ReportsOrdersTests::partsPdfLayout`. Full native gate remains RETEST_REQUIRED.

## Linux verification and limits

Full test-file syntax attempt with installed Qt failed at pre-existing `openedUrl = {};` in invoiceLinkOpensAttachedFile (QUrl assignment ambiguity); no existing test was edited to avoid it. A targeted syntax unit consists of the exact new slot, its existing includes/helpers, and a QObject-derived test class; production sources are compiled directly. This syntax check does not execute the renderer, archive or UI test. Logs: `build-single-instance-audit/dby07-native/syntax.log`, `targeted-syntax.log`.

Independent full OrderDialog rendering was not executed here; no Linux runtime RED/GREEN or Windows PASS is claimed. No DbBootstrapEnsureSchema, existing rollback test body, gate budget, PerformanceAuditScenarios.h, private data or dist changes. Archive search visibility of a separate year field is still an open DBY-07 acceptance question; the new slot verifies distinct UID selection and number display, not every search presentation requirement.

Targeted Qt/Linux syntax: PASS (exit 0). Unchanged static-only gates: PASS (exit 0). These are compilation/static evidence only. Source hashes (SOURCE_SHA256.json left for coordinated desktop update):
```text
af3ea5d174c1d616bd1e7eb04b342642be63e3d084117ef006741631b98cf174  tests/ReportsOrdersTests.cpp
d1b7b48a2f04cd87eb9efdbf39895a421bfd15786199b80d99cc91f4d3fbc01d  src/ui/common/dialogs/OrderDialogPdfFlow.cpp
f66fec854bfa8f5f1975d437dd18ff9b3595d009041755e297d699014031b7e2  src/ui/common/dialogs/OrderDialog.h
a863d7215c82d399e66c028605a4c11e83168fb5ee8c6263067cd421ca5e7b1f  src/core/services/orders/OrderDialogPdfService.cpp
8a4f585e4cad4c7c41a0bb8f7c4bcb9493977ca0d39cc1825e5257d1befd1451  src/core/services/orders/OrderDialogPdfService.h
```
