# DBY-T189 — invoice import owner selection

Task base 7db1399; integrated desktop 1d9e7d5 before publication. Last user-confirmed weekly remaining 52%; stop at <=5%. Synthetic sources only; no profile, dist, environment, gate or budget changes.

## Reproduction and change

The unchanged resolver extracted from 7db1399 reproduces three failures against SQLite: ambiguous visible 7 silently selects internal 91 using the invoice year; missing 999 returns 999; input 90 selects an order whose visible number is 7. Command `bash scripts/reproduce_invoice_owner_previous.sh` exits 1 with exactly these three failures. The historical reference is evidence only, not a startup constraint.

Invoice import now searches the active headers and archive registry by visible number, with no date preference, internal-number alternative, search-index truncation or draft creation. The existing invoice review's CSV/PDF commit flow asks for car, opening date and UID when the number has multiple owners. Selection applies to the selected invoice rows bearing that number. UID stays separate from the public number. A stale selection fails instead of being reassigned.

Before source freezing or managed copying, all owners are checked. An archive owner must exist in a readable physical archive with matching UID/visible/internal identity; an occupied internal slot rejects a conflicting owner. Existing active owners do not undergo interactive hydration. Archive owners use the established UID route. The actual mutation transaction checks all owners before receipt/row processing, checks again before attaching a newly inserted part and before financial updates. Desktop's retirement check is retained in preflight and transactional validation. No missing owner invokes ensureEditableOrder.

The original invoice_import.v1 key calculation and part-ID array remain unchanged. A prior receipt must have exactly the expected row count, unique valid IDs, matching current parent/child owner UIDs and a readable attachment with the source SHA-256. Receipt checks precede managed copying; mismatched or missing documents are not silently repaired. A valid repeat adds no rows and keeps the existing file. A different explicit importOperationId retains the intentional-reimport behavior. v1 records do not contain a historical child-UID snapshot; this increment does not invent one or claim to detect every external same-owner edit.

## Cloud evidence

- `bash scripts/reproduce_invoice_owner_previous.sh`: expected exit 1, three reproduced failures in the unchanged production resolver.
- `bash scripts/run_invoice_import_owners.sh`: PASS, zero failures. Compiles the actual production owner/receipt helpers and desktop OrderDeletionPublication implementation with Qt/SQLite; no doubles. Covers active/archived candidates, numeric collisions, absent/changed/duplicate UID, transaction rollback/retry, physical read-only archive checks, occupied slots, v1 receipt repeat/foreign child/changed bytes/duplicate IDs/row loss, retirement and SQL dependency errors.
- `moc tests/DataAttachmentSafetyTests.cpp` and `g++ -fsyntax-only` on the native test, service and review implementation with QtCore/Sql/Gui/Widgets/Network/PrintSupport/Test: PASS. Exact SDK flags follow scripts/run_invoice_import_owners.sh, with `-DQT_WIDGETS_LIB -DQT_GUI_LIB`, the listed Qt include directories and `-Ibuild-single-instance-audit`.
- `python -B scripts/run_change_gates.py --static-only`: PASS. This does not accept application behavior or release delivery.

Exact syntax command used after integration:

```bash
qt=/workspace/qt-sdk/root/usr/include/x86_64-linux-gnu/qt6
/workspace/qt-sdk/root/usr/lib/qt6/libexec/moc -Isrc -I"$qt" -I"$qt/QtCore" tests/DataAttachmentSafetyTests.cpp -o build-single-instance-audit/DataAttachmentSafetyTests.moc
g++ -std=c++17 -fPIC -DQT_WIDGETS_LIB -DQT_GUI_LIB -fsyntax-only -Isrc -Ibuild-single-instance-audit -I"$qt" -I"$qt/QtCore" -I"$qt/QtSql" -I"$qt/QtGui" -I"$qt/QtWidgets" -I"$qt/QtNetwork" -I"$qt/QtPrintSupport" -I"$qt/QtTest" tests/DataAttachmentSafetyTests.cpp src/core/services/orders/CurrentOrdersInvoiceImportService.cpp src/ui/tabs/orders/CurrentOrdersPageLocalSupport.cpp
```

The portable executable does not run the complete mutation executor, archive hydration or modal review flow. Those are covered by the native slots below and remain NOT_RUN here. Linux full runner remains limited by Windows platform dependencies. No EXE or installer delivered.

## Windows handoff

Run DataAttachmentSafetyTests with these exact slots (plus the existing reference regressions):

```text
invoiceImportArchiveOwnerRoute
invoiceImportVisibleOwnerSelectionAndNoDraft
invoiceImportReceiptOwnerAndContentGuards
invoiceImportOwnerChangeRollsBack
invoiceReviewCsvPdfOwnerSelection
invoiceImportIsAtomicAndIdempotent
metadataFailureRollsBackWholeInvoice
missingSourceDoesNotInsertRows
truncatedSourceDoesNotInsertRows
```

Use the established isolated CS_TEST_DATA_ROOT and report/storage configuration. New slots cover actual archived hydration; nonmatching invoice year and physical-number collision; exact 66.63 purchase sum; missing/ambiguous/stale owners; v1 repeated import after reopen; foreign-owner and changed-file rejection with original bytes preserved; owner mutation inside the real transaction and rollback; CSV canonical PDF and direct PDF through the existing review commit with explicit choice, cancellation, changed owner while choosing, repair and repeat. Intentional transaction failures require their exact diagnostic messages; unrelated critical diagnostics remain visible.

DBY-04/06/07 remain IN_PROGRESS. Native/full-suite results and UI presentation require Windows acceptance on the published increment. A registry pointing to an unreadable or inconsistent archive fails closed and still needs repair through the existing reconciliation workflow.
