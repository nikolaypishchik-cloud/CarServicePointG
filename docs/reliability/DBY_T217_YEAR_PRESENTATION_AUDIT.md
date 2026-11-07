# DBY-T217 — two-year presentation audit

Classification: development audit and Windows reproduction fixture, **not acceptance**.
Source inspected: a1a00db. User supplied 33% weekly remaining, stop at 25%; subsequent reply authorized only the narrow check, without a fresh percentage. No broad UI build or performance run was started. No production code, budgets, private profiles, dist or PerformanceAuditScenarios.h changed.

## Executed Qt/Linux check

`bash scripts/run_order_year_presentation_probe.sh` compiles the actual OrderSearchRepository, OrderSearchIndex and OrderDialogFlowController with a synthetic SQLite source and a real offscreen QSpinBox. Two records have public number 7: internal 101 / UID old-uid / year 2025 / archive; internal 202 / UID new-uid / year 2026 / active. Search returns both distinct identities, newest first; applying the production display controller to each shows `7` and disables editing. Search for 999 is empty. Exit 0. This is a real widget/controller check, not a test of the full OrderDialog, archive hydration or PDF renderer. No source/destination DB files are opened by the locator fixture: it is in-memory. The archive path is a synthetic locator label.

Log: build-single-instance-audit/dby07/widget-search.log:
```
"new-uid" 202 2026 "7"
"old-uid" 101 2025 "7"
PASS actual search repository returns both stable identities; actual spinbox controller displays only 7 for both
```

## Exact active-path map

| Output | Active implementation | Evidence / limitation |
|---|---|---|
| Current order table № | CurrentOrdersQueryService.cpp:692 projects order_number; internal/year/UID are separate hidden fields. CurrentOrdersPageActions obtains selected UID; CurrentOrdersCoordinator prepares that UID for open. | Source checked; controller runtime probe passes. Full table interaction with archive hydration remains native. |
| Form № | OrderDialogLifecycleFlow.cpp:320 assigns snapshot.visibleNumber to m_num; OrderDialogFlowController::applyResolvedDisplayNumber repeats that rule after save. | Real QSpinBox result is 7 for both identities. |
| History/search rows | HistoryQueryService.cpp:243 projects visible №; numeric search calls OrderSearchAggregator and filters UID candidates. OrderSearchRepository returns orderYear, UID, storage path. | Actual repository probe returns both years/UIDs. HistoryPage.cpp:132 treats every `__` field as hidden, including __order_year. Whether archive-search year is adequately shown requires the full HistoryPage interaction; do not count hidden model year as visible UI acceptance. |
| History open | HistoryPage selected row contains __internal_number and __order_uid; prepareOrderForOpen routes the UID before opening/reopening. | Source trace, not new native PASS. |
| Customer/parts PDF content | OrderDialog::exportCustomerReportToFile / exportPartsReportToFile → exportPdfToFile in OrderDialogPdfFlow.cpp. Detail rows come from canonicalOrderUidForInternalNumber and UID-based print queries. | **Concrete source mismatch:** line 451 prints `QString::number(m_orderNumber)` at «№ наряду», although the form shows m_num. For 101/202 both visible 7, the source expression is 101/202. Full render not executed in this narrow Linux check. |
| PDF preview filenames | OrderDialogPdfFlow.cpp:112 passes internal m_orderNumber to OrderDialogPdfService; service formats `<number>_<dd.MM.yyyy>[_parts_report].pdf`. StorageDirectoriesGuard uses the configured PDF root directly. | Same internal/public mismatch in filename argument. Dates distinguish these two fixtures, so this is not evidence of overwrite. Date year is a calendar date, not an illicit `/year` suffix on the public number. |
| Report list/period/parts PDFs | Reports readers carry visible number plus separate internal identity; PartsReportQuery and OrderDialogPrintQueryService select children by UID. | Existing `orderListAndPrintData`, `periodPdfFailuresAndUidRows`, `partsPdfIdentityOwnersAttachmentsAndStock`; these do not prove OrderDialog header/preview filename. |
| Invoice selection | InvoiceImportOwnerSupport candidates include UID/car/date; invoiceReviewCsvPdfOwnerSelection tests explicit selection. | Existing native source coverage, not rerun here. |

No claim that the two PDF mismatches were reproduced by a full renderer in Linux. They are actionable source findings for the native reproduction below. No speculative production change was applied under the requested narrow-check scope. DBY-07 stays OPEN.

## Native Windows fixture: exact test entry and acceptance

Proposed slot **ReportsOrdersTests::annualOrderUiPdfVisibleIdentity** (not registered or claimed PASS in this increment). Reuse the isolated `partsPdfLayout` initialization: QTemporaryDir, test-mode QSettings, prepareRuntime/openPrepared/ensureSchema/rebuildRouterViews, booking/cloudsync disabled, test owner actor. Do not run against a user's profile. Use the existing bundled poppler discovery used in DataAttachmentSafetyTests; missing extractor is an explicit failure, not a skip.

Fixture after initialization (unique marker prefix allows inspection and cleanup):
```sql
INSERT INTO masters(id,name,role) VALUES('year-pdf-master','Synthetic master','master');
INSERT INTO work_orders(number,order_number,order_uid,order_year,opened_at,status,plate,plate_norm,car,client,master_id)
VALUES(900101,7,'year-pdf-2025',2025,'2025-02-03T12:00:00','open','TEST-A','TESTA','Synthetic A','Synthetic','year-pdf-master'),
      (900202,7,'year-pdf-2026',2026,'2026-02-03T12:00:00','open','TEST-B','TESTB','Synthetic B','Synthetic','year-pdf-master');
INSERT INTO work_order_parts(order_number,order_uid,ordered_at,name,qty,price,amount,purchase_price,purchase_amount)
VALUES(900101,'year-pdf-2025','2025-02-03','PART-ONLY-2025',1,123.45,123.45,100,100),
      (900202,'year-pdf-2026','2026-02-03','PART-ONLY-2026',1,234.56,234.56,200,200);
```

Test body following the above setup (existing ReportsOrdersTests includes OrderDialog; add OrderDialogPdfService/QProcess/QRegularExpression includes as needed):
```cpp
Database::setSetting("settings.int.directories.pdf_dir", dir.path());
QStringList failures;
const QString poppler = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
    "../resources/windows/ocr-layer/poppler/bin/pdftotext.exe");
QVERIFY2(QFileInfo::exists(poppler), "Bundled pdftotext is required");
for (int year : {2025, 2026}) {
    const int internal = year == 2025 ? 900101 : 900202;
    OrderDialog dialog(nullptr, internal);
    dialog.show();
    QCoreApplication::processEvents();
    // Public renderer: no preview viewer launch and no click through external prompts.
    for (bool parts : {false, true}) {
        const QString file = dir.filePath(QString("render-%1-%2.pdf").arg(year).arg(parts));
        QString error;
        const bool ok = parts ? dialog.exportPartsReportToFile(file, &error)
                              : dialog.exportCustomerReportToFile(file, QString(), &error);
        QVERIFY2(ok, qPrintable(error));
        QProcess extract;
        extract.start(poppler, {"-layout", "-enc", "UTF-8", file, "-"});
        QVERIFY(extract.waitForFinished()); QCOMPARE(extract.exitCode(), 0);
        const auto text = QString::fromUtf8(extract.readAllStandardOutput());
        const QRegularExpression number(QString::fromUtf8("№\\s*наряду\\s+7\\b"));
        if (!number.match(text).hasMatch()) failures << QString("wrong PDF number %1/%2").arg(year).arg(parts);
        QVERIFY(text.contains(QString("PART-ONLY-%1").arg(year)));
        QVERIFY(!text.contains(QString("PART-ONLY-%1").arg(year == 2025 ? 2026 : 2025)));
        // Execute the actual preview-path service with the argument used by the
        // current UI caller. Refuse export deliberately to avoid external viewers.
        QString captured;
        const auto preview = OrderDialogPdfService::exportPreviewAndOpen(internal, &dialog,
            [&](const QString& path, QString* e) { captured=path; *e="synthetic path capture"; return false; },
            parts, QDate(year,2,3));
        QVERIFY(!preview.ok); QCOMPARE(preview.error, QString("synthetic path capture"));
        const auto expected = QString("7_03.02.%1%2.pdf").arg(year).arg(parts?"_parts_report":"");
        if (QFileInfo(captured).fileName()!=expected) failures << QString("wrong preview filename %1").arg(captured);
    }
}
QVERIFY2(failures.isEmpty(), qPrintable(failures.join('\n')));
```

This fixture intentionally asserts the user requirement, not the currently suspected wrong values. Expected RED on a1a00db; Windows must actually record it. It is supplied as a reproduction recipe rather than registering an unexecuted failing slot in the acceptance suite. When implementing the confirmed fix, update the fixture's preview invocation to the public API/caller contract actually chosen; do not weaken the expected visible filename. Preserve UID lookup for details and internal key for financial/permission checks: replacing all m_orderNumber uses would be wrong.

For interactive archive acceptance, close and archive the 2025 record using the normal service, reopen HistoryPage, search 7 across both years, assert two distinguishable results and select each in turn. Confirm recoveryOrderUid equals the chosen UID and the corresponding unique part marker is shown. Verify the order year is visibly available in archive search, and absent from the normal number field. Repeat after restart. This requires full app/native runtime and is NOT covered by the in-memory locator probe.

Static verification: `python3 -B scripts/run_change_gates.py --static-only`, exit 0. Exact portable fixture hashes:
```text
41423adfe8b9fbc1f2b5bb750aedf40c1f56f2602819e4ff74a63d2cfcc9ffa1  tests/OrderYearPresentationProbe.cpp
6d0f03cfb01c7ea547e463cf1ad3f32377dc2e076411d920a20697abc1a15125  scripts/run_order_year_presentation_probe.sh
```
