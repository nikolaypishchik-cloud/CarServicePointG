// Extracted from src/infrastructure/db/sqlite/DbImportCatalogFromOrders.cpp for P1/P2 monolith reduction.
// Included exactly once by the original translation unit/header; do not compile separately.

                                                    mergedVehicleModel,
                                                    mergedYearFrom,
                                                    mergedYearTo,
                                                    mergedFuel,
                                                    mergedEngine,
                                                    mergedBody,
                                                    mergedGroup,
                                                    mergedPartNumber,
                                                    mergedOem,
                                                    mergedName,
                                                    mergedBrandName,
                                                    mergedSupplier,
                                                    mergedNote,
                                                    sourceFirst,
                                                    sourceLast,
                                                    firstImportedAt,
                                                    rec,
                                                    agg);
            if (!updateOp.ok) {
                ++report->errorCount;
                for (const CatalogSourceWorkRow& row : agg.sources) {
                    recordDecisionForRow(row,
                                         QStringLiteral("error"),
                                         QStringLiteral("db_update_error"),
                                         updateOp.errorText,
                                         QStringLiteral("fix_db_or_retry"),
                                         false,
                                         QList<int>{ rec.id },
                                         agg.dedupeMode,
                                         &errorDetails,
                                         &errorReasonCounts,
                                         classifyCandidateStrength(agg),
                                         QStringLiteral("update"),
                                         updateOp.errorText,
                                         QStringLiteral("SQL: ") + updateOp.sqlText + QStringLiteral(" | Native: ") + updateOp.nativeErrorCode + QStringLiteral(" | Binds: ") + updateOp.bindSnapshot);
                    logImportItem(row, QStringLiteral("error"), rec.id, updateOp.errorText, agg.dedupeMode);
                }
            } else {
                ++report->updatedCount;
                ++report->mergedCount;
                linkedSourceCount += agg.sources.size();
                saveMaps(rec.id, agg, false);
                rec.partNumber = mergedPartNumber;
                rec.oem = mergedOem;
                rec.normalizedPartNumber = agg.normalizedPartNumber;
                rec.normalizedOemNumber = agg.normalizedOemNumber;
                rec.name = mergedName;
                rec.normalizedName = agg.normalizedName;
                rec.brandName = mergedBrandName;
                rec.supplier = mergedSupplier;
                rec.normalizedStore = normalizedCatalogStoreKey(mergedSupplier);
                rec.catalogMatchKey = agg.catalogMatchKey;
                rec.dedupeSignature = agg.dedupeSignature;
                existing[chosenExistingIndex] = rec;
                if (!rec.catalogMatchKey.isEmpty()) byMatchKey.insert(rec.catalogMatchKey, chosenExistingIndex);
                if (!rec.normalizedOemNumber.isEmpty()) byNormOem.insert(rec.normalizedOemNumber, chosenExistingIndex);
                if (!rec.normalizedPartNumber.isEmpty()) byNormPart.insert(rec.normalizedPartNumber, chosenExistingIndex);
                if (!rec.normalizedName.isEmpty() && !normalizedCatalogLookupKey(rec.brandName).isEmpty()) byBrandName.insert(normalizedCatalogLookupKey(rec.brandName) + QStringLiteral("|") + rec.normalizedName, chosenExistingIndex);
                if (!rec.normalizedName.isEmpty()) byNameStore.insert(rec.normalizedName + QStringLiteral("|") + rec.normalizedStore, chosenExistingIndex);
                for (const CatalogSourceWorkRow& row : agg.sources) {
                    recordDecisionForRow(row,
                                         QStringLiteral("update"),
                                         QStringLiteral("updated_existing"),
                                         QString::fromUtf8("Існуючу картку каталогу безпечно оновлено."),
                                         QStringLiteral("updated_existing"),
                                         false,
                                         QList<int>{ rec.id },
                                         agg.dedupeMode,
                                         &updatedDetails,
                                         nullptr,
                                         classifyCandidateStrength(agg));
                    recordDecisionForRow(row,
                                         QStringLiteral("merge"),
                                         QStringLiteral("merged_into_existing"),
                                         QString::fromUtf8("Джерело об'єднано з наявною карткою каталогу без втрати даних."),
                                         QStringLiteral("merged_into_existing"),
                                         false,
                                         QList<int>{ rec.id },
                                         agg.dedupeMode,
                                         &mergedDetails,
                                         nullptr,
                                         classifyCandidateStrength(agg));
                    logImportItem(row, QStringLiteral("update"), rec.id, QString::fromUtf8("Оновлено існуючу картку каталогу."), agg.dedupeMode);
                }
            }
            continue;
        }

        CatalogDbOperationResult insertOp = schemaAwareInsertCatalog(agg, importedNote);
        if (!insertOp.ok) {
            ++report->errorCount;
            for (const CatalogSourceWorkRow& row : agg.sources) {
                recordDecisionForRow(row,
                                     QStringLiteral("error"),
                                     QStringLiteral("db_insert_error"),
                                     insertOp.errorText,
                                     QStringLiteral("fix_db_or_retry"),
                                     false,
                                     QList<int>(),
                                     agg.dedupeMode,
                                     &errorDetails,
                                     &errorReasonCounts,
                                     classifyCandidateStrength(agg),
                                     QStringLiteral("insert"),
                                     insertOp.errorText,
                                     QStringLiteral("SQL: ") + insertOp.sqlText + QStringLiteral(" | Native: ") + insertOp.nativeErrorCode + QStringLiteral(" | Binds: ") + insertOp.bindSnapshot);
                logImportItem(row, QStringLiteral("error"), 0, insertOp.errorText, agg.dedupeMode);
            }
        } else {
            const int catalogId = static_cast<int>(insertOp.lastInsertId);
            if (catalogId <= 0) {
                ++report->errorCount;
                for (const CatalogSourceWorkRow& row : agg.sources) {
                    recordDecisionForRow(row,
                                         QStringLiteral("error"),
                                         QStringLiteral("insert_missing_last_insert_id"),
                                         QString::fromUtf8("Після INSERT не вдалося визначити id створеної картки каталогу."),
                                         QStringLiteral("retry_or_check_schema"),
                                         false,
                                         QList<int>(),
                                         agg.dedupeMode,
                                         &errorDetails,
                                         &errorReasonCounts,
                                         classifyCandidateStrength(agg),
                                         QStringLiteral("insert"),
                                         insertOp.errorText,
                                         QStringLiteral("SQL: ") + insertOp.sqlText + QStringLiteral(" | Native: ") + insertOp.nativeErrorCode + QStringLiteral(" | Binds: ") + insertOp.bindSnapshot);
                    logImportItem(row, QStringLiteral("error"), 0, QString::fromUtf8("INSERT succeeded but no catalog id was returned."), agg.dedupeMode);
                }
                continue;
            }
            ++report->addedCount;
            linkedSourceCount += agg.sources.size();
            saveMaps(catalogId, agg, true);
            ExistingCatalogRecord rec;
            rec.id = catalogId;
            rec.partNumber = agg.partNumber;
            rec.oem = agg.oem;
            rec.normalizedPartNumber = agg.normalizedPartNumber;
            rec.normalizedOemNumber = agg.normalizedOemNumber;
            rec.name = agg.name;
            rec.normalizedName = agg.normalizedName;
            rec.brandName = agg.brandName;
            rec.supplier = agg.supplier;
            rec.normalizedStore = normalizedCatalogStoreKey(agg.supplier);
            rec.catalogMatchKey = agg.catalogMatchKey;
            rec.dedupeSignature = agg.dedupeSignature;
            existingIndexById.insert(catalogId, existing.size());
            const int idx = existing.size();
            existing.push_back(rec);
            if (!rec.catalogMatchKey.isEmpty()) byMatchKey.insert(rec.catalogMatchKey, idx);
            if (!rec.normalizedOemNumber.isEmpty()) byNormOem.insert(rec.normalizedOemNumber, idx);
            if (!rec.normalizedPartNumber.isEmpty()) byNormPart.insert(rec.normalizedPartNumber, idx);
            if (!rec.normalizedName.isEmpty() && !normalizedCatalogLookupKey(rec.brandName).isEmpty()) byBrandName.insert(normalizedCatalogLookupKey(rec.brandName) + QStringLiteral("|") + rec.normalizedName, idx);
            if (!rec.normalizedName.isEmpty()) byNameStore.insert(rec.normalizedName + QStringLiteral("|") + rec.normalizedStore, idx);
            for (const CatalogSourceWorkRow& row : agg.sources) {
                recordDecisionForRow(row,
                                     QStringLiteral("create"),
                                     QStringLiteral("created_new"),
                                     QString::fromUtf8("Створено нову картку каталогу."),
                                     QStringLiteral("created_new"),
                                     false,
                                     QList<int>{ catalogId },
                                     agg.dedupeMode,
                                     &createdDetails,
                                     nullptr,
                                     classifyCandidateStrength(agg));
                logImportItem(row, QStringLiteral("insert"), catalogId, QString::fromUtf8("Створено нову картку каталогу."), agg.dedupeMode);
            }
        }
    }

    report->skippedCount = skippedDetails.size();
    report->errorCount = errorDetails.size();
    report->conflictCount = conflictDetails.size();
    const int createdSourceRows = createdDetails.size();
    const int updatedSourceRows = updatedDetails.size();
    const int mergedSourceRows = mergedDetails.size();
    const int alreadyCoveredSourceRows = skippedReasonCounts.value(QStringLiteral("duplicate_no_new_data"), 0);
    const int realSkippedSourceRows = qMax(0, report->skippedCount - alreadyCoveredSourceRows);
    const int missingCount = qMax(0, report->eligibleCount - linkedSourceCount);

    auto appendReasonLines = [](QStringList* out, const QString& title, const QMap<QString, int>& counts) {
        if (!out || counts.isEmpty()) return;
        out->append(title);
        for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
            out->append(QStringLiteral("  - ") + it.key() + QStringLiteral(": ") + QString::number(it.value()));
        }
    };

    QStringList summaryLines;
    summaryLines << QString::fromUtf8("Імпорт у каталог завершено.")
                 << QString::fromUtf8("Опрацьовано рядків: ") + QString::number(report->processedCount)
                 << QString::fromUtf8("Валідних джерельних рядків: ") + QString::number(report->eligibleCount)
                 << QString::fromUtf8("Унікальних підписів джерел: ") + QString::number(report->uniqueSourceCount)
                 << QString::fromUtf8("Унікальних каталожних кандидатів: ") + QString::number(aggregates.size())
                 << QString::fromUtf8("Створено нових карток: ") + QString::number(report->addedCount)
                 << QString::fromUtf8("Оновлено існуючих карток: ") + QString::number(report->updatedCount)
                 << QString::fromUtf8("Успішно об’єднано: ") + QString::number(report->mergedCount)
                 << QString::fromUtf8("Конфлікти даних: ") + QString::number(report->conflictCount)
                 << QString::fromUtf8("Вже покрито наявними картками: ") + QString::number(alreadyCoveredSourceRows)
                 << QString::fromUtf8("Свідомо пропущено: ") + QString::number(realSkippedSourceRows)
                 << QString::fromUtf8("Технічних помилок: ") + QString::number(report->errorCount)
                 << QString::fromUtf8("Створено джерельних рядків: ") + QString::number(createdSourceRows)
                 << QString::fromUtf8("Оновлено джерельних рядків: ") + QString::number(updatedSourceRows)
                 << QString::fromUtf8("Об’єднано джерельних рядків: ") + QString::number(mergedSourceRows)
                 << QString::fromUtf8("Джерела з part number: ") + QString::number(withPartNumberCount)
                 << QString::fromUtf8("Записи лише з назвою: ") + QString::number(nameOnlyCount)
                 << QString::fromUtf8("OEM/part кандидати: ") + QString::number(withOemCount)
                 << QString::fromUtf8("Рядки з active-джерела: ") + QString::number(report->activeImportedCount)
                 << QString::fromUtf8("Рядки з runtime/legacy-джерел: ") + QString::number(report->legacyImportedCount)
                 << QString::fromUtf8("Невкритих валідних джерельних рядків: ") + QString::number(missingCount);
    appendReasonLines(&summaryLines, QString::fromUtf8("Категорії пропусків:"), skippedReasonCounts);
    appendReasonLines(&summaryLines, QString::fromUtf8("Категорії конфліктів:"), conflictReasonCounts);
    appendReasonLines(&summaryLines, QString::fromUtf8("Категорії помилок:"), errorReasonCounts);
    summaryLines << QString::fromUtf8("Пакет звітів: ") + reportsDir;
    report->summaryText = summaryLines.join(QStringLiteral("\n"));

    const QString summaryPath = QDir(reportsDir).filePath(QStringLiteral("CatalogImportSummary.md"));
    const QString summaryJsonPath = QDir(reportsDir).filePath(QStringLiteral("CatalogImportSummary.json"));
    const QString summaryTxtPath = QDir(reportsDir).filePath(QStringLiteral("CatalogImportSummary.txt"));
    const QString coveragePath = QDir(reportsDir).filePath(QStringLiteral("CatalogImportCoverageReport.md"));
    const QString conflictsJsonPath = QDir(reportsDir).filePath(QStringLiteral("CatalogImportConflicts.json"));
    const QString conflictsCsvPath = QDir(reportsDir).filePath(QStringLiteral("CatalogImportConflicts.csv"));
    const QString skippedJsonPath = QDir(reportsDir).filePath(QStringLiteral("CatalogImportSkippedDetails.json"));
    const QString skippedCsvPath = QDir(reportsDir).filePath(QStringLiteral("CatalogImportSkippedDetails.csv"));
    const QString errorsJsonPath = QDir(reportsDir).filePath(QStringLiteral("CatalogImportErrors.json"));
    const QString errorsCsvPath = QDir(reportsDir).filePath(QStringLiteral("CatalogImportErrors.csv"));
    const QString createdCsvPath = QDir(reportsDir).filePath(QStringLiteral("CatalogImportSuccessCreated.csv"));
    const QString updatedCsvPath = QDir(reportsDir).filePath(QStringLiteral("CatalogImportSuccessUpdated.csv"));
    const QString mergedCsvPath = QDir(reportsDir).filePath(QStringLiteral("CatalogImportMerged.csv"));
    const QString conflictsDetailsCsvPath = QDir(reportsDir).filePath(QStringLiteral("CatalogImportConflictsDetails.csv"));
    const QString sourceToCandidateCsvPath = QDir(reportsDir).filePath(QStringLiteral("CatalogImportSourceToCandidateMap.csv"));
    const QString normalizedAuditCsvPath = QDir(reportsDir).filePath(QStringLiteral("CatalogImportNormalizedFieldsAudit.csv"));

    auto detailListToJson = [](const QVector<CatalogImportDecisionDetail>& details) {
        QJsonArray arr;
        for (const CatalogImportDecisionDetail& detail : details) {
            QJsonObject obj;
            obj.insert(QStringLiteral("source_part_id"), detail.sourcePartId);
            obj.insert(QStringLiteral("order_number"), detail.orderNumber);
            obj.insert(QStringLiteral("order_uid"), detail.orderUid);
            obj.insert(QStringLiteral("source_db_layer"), detail.sourceDbLayer);
            obj.insert(QStringLiteral("part_number"), detail.partNumber);
            obj.insert(QStringLiteral("oem_number"), detail.oemNumber);
            obj.insert(QStringLiteral("name"), detail.name);
            obj.insert(QStringLiteral("supplier"), detail.supplier);
            obj.insert(QStringLiteral("note"), detail.note);
            obj.insert(QStringLiteral("normalized_part_number"), detail.normalizedPartNumber);
            obj.insert(QStringLiteral("normalized_oem_number"), detail.normalizedOemNumber);
            obj.insert(QStringLiteral("normalized_name"), detail.normalizedName);
            obj.insert(QStringLiteral("normalized_store"), detail.normalizedStore);
            obj.insert(QStringLiteral("dedupe_mode"), detail.dedupeMode);
            obj.insert(QStringLiteral("catalog_match_key"), detail.catalogMatchKey);
            obj.insert(QStringLiteral("source_signature"), detail.sourceSignature);
            obj.insert(QStringLiteral("decision"), detail.decision);
            obj.insert(QStringLiteral("reason_code"), detail.reasonCode);
            obj.insert(QStringLiteral("reason_text"), detail.reasonText);
            obj.insert(QStringLiteral("recommended_action"), detail.recommendedAction);
            obj.insert(QStringLiteral("candidate_class"), detail.candidateClass);
            obj.insert(QStringLiteral("sql_operation"), detail.sqlOperation);
            obj.insert(QStringLiteral("sql_error_text"), detail.sqlErrorText);
            obj.insert(QStringLiteral("bind_snapshot"), detail.bindSnapshot);
            obj.insert(QStringLiteral("can_import_after_relax"), detail.canImportAfterRelax);
            QJsonArray idsArr;
            for (int id : detail.candidateCatalogIds) idsArr.append(id);
            obj.insert(QStringLiteral("candidate_catalog_ids"), idsArr);
            arr.append(obj);
        }
        return arr;
    };
    auto detailListToCsv = [](const QVector<CatalogImportDecisionDetail>& details) {
        QStringList lines;
        lines << QStringLiteral("source_part_id,order_number,order_uid,source_db_layer,part_number,oem_number,name,supplier,note,normalized_part_number,normalized_oem_number,normalized_name,normalized_store,dedupe_mode,catalog_match_key,source_signature,decision,reason_code,reason_text,recommended_action,candidate_class,sql_operation,sql_error_text,bind_snapshot,can_import_after_relax,candidate_catalog_ids");
        auto q = [](QString v) {
            v.replace(QStringLiteral("\""), QStringLiteral("\"\""));
            return QStringLiteral("\"") + v + QStringLiteral("\"");
        };
        for (const CatalogImportDecisionDetail& d : details) {
            QStringList ids;
            for (int id : d.candidateCatalogIds) ids << QString::number(id);
            lines << QStringList({
                QString::number(d.sourcePartId),
                QString::number(d.orderNumber),
                q(d.orderUid),
                q(d.sourceDbLayer),
                q(d.partNumber),
                q(d.oemNumber),
                q(d.name),
                q(d.supplier),
                q(d.note),
                q(d.normalizedPartNumber),
                q(d.normalizedOemNumber),
                q(d.normalizedName),
                q(d.normalizedStore),
                q(d.dedupeMode),
                q(d.catalogMatchKey),
                q(d.sourceSignature),
                q(d.decision),
                q(d.reasonCode),
                q(d.reasonText),
                q(d.recommendedAction),
                q(d.candidateClass),
                q(d.sqlOperation),
                q(d.sqlErrorText),
                q(d.bindSnapshot),
                QString::number(d.canImportAfterRelax ? 1 : 0),
                q(ids.join(QStringLiteral("|")))
            }).join(QStringLiteral(","));
        }
        return lines.join(QStringLiteral("\n"));
    };

    QString summaryMd;
    summaryMd += QStringLiteral("# Catalog import summary\n\n");
    summaryMd += QStringLiteral("- Batch: `") + batchId + QStringLiteral("`\n");
    summaryMd += QStringLiteral("- Processed rows: ") + QString::number(report->processedCount) + QStringLiteral("\n");
    summaryMd += QStringLiteral("- Eligible rows: ") + QString::number(report->eligibleCount) + QStringLiteral("\n");
    summaryMd += QStringLiteral("- Unique source signatures: ") + QString::number(report->uniqueSourceCount) + QStringLiteral("\n");
    summaryMd += QStringLiteral("- Unique catalog candidates: ") + QString::number(aggregates.size()) + QStringLiteral("\n");
    summaryMd += QStringLiteral("- Added: ") + QString::number(report->addedCount) + QStringLiteral("\n");
    summaryMd += QStringLiteral("- Updated: ") + QString::number(report->updatedCount) + QStringLiteral("\n");
    summaryMd += QStringLiteral("- Merged: ") + QString::number(report->mergedCount) + QStringLiteral("\n");
    summaryMd += QStringLiteral("- Skipped: ") + QString::number(report->skippedCount) + QStringLiteral("\n");
    summaryMd += QStringLiteral("- Conflicts: ") + QString::number(report->conflictCount) + QStringLiteral("\n");
    summaryMd += QStringLiteral("- Errors: ") + QString::number(report->errorCount) + QStringLiteral("\n");
    summaryMd += QStringLiteral("- Active rows: ") + QString::number(report->activeImportedCount) + QStringLiteral("\n");
    summaryMd += QStringLiteral("- Runtime hydrated rows: ") + QString::number(report->legacyImportedCount) + QStringLiteral("\n");
    summaryMd += QStringLiteral("- Archive rows: ") + QString::number(report->archiveImportedCount) + QStringLiteral("\n");
    summaryMd += QStringLiteral("- Reports dir: `") + reportsDir + QStringLiteral("`\n");
    if (!skippedReasonCounts.isEmpty()) {
        summaryMd += QStringLiteral("\n## Skip reasons\n\n");
        for (auto it = skippedReasonCounts.constBegin(); it != skippedReasonCounts.constEnd(); ++it) {
            summaryMd += QStringLiteral("- `") + it.key() + QStringLiteral("`: ") + QString::number(it.value()) + QStringLiteral("\n");
        }
    }
    if (!errorReasonCounts.isEmpty()) {
        summaryMd += QStringLiteral("\n## Error reasons\n\n");
        for (auto it = errorReasonCounts.constBegin(); it != errorReasonCounts.constEnd(); ++it) {
            summaryMd += QStringLiteral("- `") + it.key() + QStringLiteral("`: ") + QString::number(it.value()) + QStringLiteral("\n");
        }
    }

    QString coverageMd;
    coverageMd += QStringLiteral("# Catalog import coverage report\n\n");
    coverageMd += QStringLiteral("- Source rows total: ") + QString::number(report->processedCount) + QStringLiteral("\n");
    coverageMd += QStringLiteral("- Eligible rows: ") + QString::number(report->eligibleCount) + QStringLiteral("\n");
    coverageMd += QStringLiteral("- Linked rows: ") + QString::number(linkedSourceCount) + QStringLiteral("\n");
    coverageMd += QStringLiteral("- Missing rows after import: ") + QString::number(missingCount) + QStringLiteral("\n");
    coverageMd += QStringLiteral("- Rows with part number: ") + QString::number(withPartNumberCount) + QStringLiteral("\n");
    coverageMd += QStringLiteral("- Name-only rows: ") + QString::number(nameOnlyCount) + QStringLiteral("\n");
    coverageMd += QStringLiteral("- OEM candidates: ") + QString::number(withOemCount) + QStringLiteral("\n");
    if (!skippedReasonCounts.isEmpty()) {
        coverageMd += QStringLiteral("\n## Skip reasons\n\n");
        for (auto it = skippedReasonCounts.constBegin(); it != skippedReasonCounts.constEnd(); ++it) {
            coverageMd += QStringLiteral("- `") + it.key() + QStringLiteral("`: ") + QString::number(it.value()) + QStringLiteral("\n");
        }
    }
    if (!errorReasonCounts.isEmpty()) {
        coverageMd += QStringLiteral("\n## Error reasons\n\n");
        for (auto it = errorReasonCounts.constBegin(); it != errorReasonCounts.constEnd(); ++it) {
            coverageMd += QStringLiteral("- `") + it.key() + QStringLiteral("`: ") + QString::number(it.value()) + QStringLiteral("\n");
        }
    }
    if (!skippedDetails.isEmpty()) {
        coverageMd += QStringLiteral("\n## Skipped rows (first 50)\n\n");
        const int limit = qMin(50, skippedDetails.size());
        for (int i = 0; i < limit; ++i) {
            const CatalogImportDecisionDetail& detail = skippedDetails.at(i);
            coverageMd += QStringLiteral("- part_id=") + QString::number(detail.sourcePartId)
                + QStringLiteral(", order=") + QString::number(detail.orderNumber)
                + QStringLiteral(", layer=") + detail.sourceDbLayer
                + QStringLiteral(", reason=") + detail.reasonCode
                + QStringLiteral(", name=") + detail.name
                + QStringLiteral("\n");
        }
    }

    auto csvQuote = [](QString v) {
        v.replace(QStringLiteral("\""), QStringLiteral("\"\""));
        return QStringLiteral("\"") + v + QStringLiteral("\"");
    };
    auto buildSourceToCandidateCsv = [&](const QVector<CatalogImportDecisionDetail>& details) {
        QStringList lines;
        lines << QStringLiteral("source_part_id,order_number,order_uid,source_db_layer,decision,reason_code,candidate_class,candidate_catalog_ids,catalog_match_key,source_signature");
        for (const CatalogImportDecisionDetail& d : details) {
            QStringList ids;
            for (int id : d.candidateCatalogIds) ids << QString::number(id);
            lines << QStringList({QString::number(d.sourcePartId), QString::number(d.orderNumber), csvQuote(d.orderUid), csvQuote(d.sourceDbLayer), csvQuote(d.decision), csvQuote(d.reasonCode), csvQuote(d.candidateClass), csvQuote(ids.join(QStringLiteral("|"))), csvQuote(d.catalogMatchKey), csvQuote(d.sourceSignature)}).join(QStringLiteral(","));
        }
        return lines.join(QStringLiteral("\n"));
    };
    auto buildNormalizedAuditCsv = [&](const QVector<CatalogSourceWorkRow>& rows) {
        QStringList lines;
        lines << QStringLiteral("source_part_id,order_number,order_uid,source_db_layer,part_number,oem_number,name,supplier,normalized_part_number,normalized_oem_number,normalized_name,normalized_store,catalog_match_key,source_signature");
        for (const CatalogSourceWorkRow& r : rows) {
            lines << QStringList({QString::number(r.sourcePartId), QString::number(r.orderNumber), csvQuote(r.orderUid), csvQuote(r.sourceDbLayer), csvQuote(r.partNumber), csvQuote(r.oem), csvQuote(r.name), csvQuote(r.supplier), csvQuote(r.normalizedPartNumber), csvQuote(r.normalizedOemNumber), csvQuote(r.normalizedName), csvQuote(r.normalizedStore), csvQuote(r.catalogMatchKey), csvQuote(r.dedupeSignature)}).join(QStringLiteral(","));
        }
        return lines.join(QStringLiteral("\n"));
    };

    QVector<CatalogImportDecisionDetail> allDecisionDetails = createdDetails;
    allDecisionDetails += updatedDetails;
    allDecisionDetails += mergedDetails;
    allDecisionDetails += conflictDetails;
    allDecisionDetails += skippedDetails;
    allDecisionDetails += errorDetails;
    QVector<CatalogSourceWorkRow> auditRows = sourceRows;
    auditRows += skippedInvalidRows;

    QJsonObject summaryJson;
    summaryJson.insert(QStringLiteral("import_batch_id"), batchId);
    summaryJson.insert(QStringLiteral("processed_count"), report->processedCount);
    summaryJson.insert(QStringLiteral("eligible_count"), report->eligibleCount);
    summaryJson.insert(QStringLiteral("unique_source_count"), report->uniqueSourceCount);
    summaryJson.insert(QStringLiteral("unique_catalog_candidates"), static_cast<int>(aggregates.size()));
    summaryJson.insert(QStringLiteral("added_count"), report->addedCount);
    summaryJson.insert(QStringLiteral("updated_count"), report->updatedCount);
    summaryJson.insert(QStringLiteral("merged_count"), report->mergedCount);
    summaryJson.insert(QStringLiteral("skipped_count"), report->skippedCount);
    summaryJson.insert(QStringLiteral("conflict_count"), report->conflictCount);
    summaryJson.insert(QStringLiteral("error_count"), report->errorCount);
    summaryJson.insert(QStringLiteral("created_source_rows"), createdSourceRows);
    summaryJson.insert(QStringLiteral("updated_source_rows"), updatedSourceRows);
    summaryJson.insert(QStringLiteral("merged_source_rows"), mergedSourceRows);
    summaryJson.insert(QStringLiteral("active_source_rows"), report->activeImportedCount);
    summaryJson.insert(QStringLiteral("runtime_legacy_source_rows"), report->legacyImportedCount);
    summaryJson.insert(QStringLiteral("missing_eligible_rows"), missingCount);
    auto mapToJsonObject = [](const QMap<QString, int>& counts) {
        QJsonObject obj;
        for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) obj.insert(it.key(), it.value());
        return obj;
    };
    summaryJson.insert(QStringLiteral("skip_reasons"), mapToJsonObject(skippedReasonCounts));
    summaryJson.insert(QStringLiteral("conflict_reasons"), mapToJsonObject(conflictReasonCounts));
    summaryJson.insert(QStringLiteral("error_reasons"), mapToJsonObject(errorReasonCounts));
    summaryJson.insert(QStringLiteral("reports_dir"), reportsDir);

    writeUtf8TextFile(summaryPath, summaryMd);
    writeUtf8TextFile(summaryTxtPath, report->summaryText);
    writeUtf8TextFile(summaryJsonPath, QString::fromUtf8(QJsonDocument(summaryJson).toJson(QJsonDocument::Indented)));
    writeUtf8TextFile(coveragePath, coverageMd);
    writeUtf8TextFile(conflictsCsvPath, conflictCsv.join(QStringLiteral("\n")));
    writeUtf8TextFile(conflictsJsonPath, QString::fromUtf8(QJsonDocument(conflictJson).toJson(QJsonDocument::Indented)));
    writeUtf8TextFile(conflictsDetailsCsvPath, detailListToCsv(conflictDetails));
    writeUtf8TextFile(createdCsvPath, detailListToCsv(createdDetails));
    writeUtf8TextFile(updatedCsvPath, detailListToCsv(updatedDetails));
    writeUtf8TextFile(mergedCsvPath, detailListToCsv(mergedDetails));
    writeUtf8TextFile(skippedCsvPath, detailListToCsv(skippedDetails));
    writeUtf8TextFile(skippedJsonPath, QString::fromUtf8(QJsonDocument(detailListToJson(skippedDetails)).toJson(QJsonDocument::Indented)));
    writeUtf8TextFile(errorsCsvPath, detailListToCsv(errorDetails));
    writeUtf8TextFile(errorsJsonPath, QString::fromUtf8(QJsonDocument(detailListToJson(errorDetails)).toJson(QJsonDocument::Indented)));
    writeUtf8TextFile(sourceToCandidateCsvPath, buildSourceToCandidateCsv(allDecisionDetails));
    writeUtf8TextFile(normalizedAuditCsvPath, buildNormalizedAuditCsv(auditRows));

    QSqlQuery runUpdate(d);
    runUpdate.prepare(QStringLiteral("UPDATE catalog_import_runs SET finished_at=?, processed_count=?, eligible_count=?, unique_source_count=?, added_count=?, updated_count=?, merged_count=?, skipped_count=?, skipped_oil_count=?, error_count=?, conflict_count=?, legacy_imported_count=?, archive_imported_count=?, active_imported_count=?, reports_dir=?, summary_text=? WHERE id=?"));
    runUpdate.addBindValue(nowText);
    runUpdate.addBindValue(report->processedCount);
    runUpdate.addBindValue(report->eligibleCount);
    runUpdate.addBindValue(report->uniqueSourceCount);
    runUpdate.addBindValue(report->addedCount);
    runUpdate.addBindValue(report->updatedCount);
    runUpdate.addBindValue(report->mergedCount);
    runUpdate.addBindValue(report->skippedCount);
    runUpdate.addBindValue(report->skippedOilCount);
    runUpdate.addBindValue(report->errorCount);
    runUpdate.addBindValue(report->conflictCount);
    runUpdate.addBindValue(report->legacyImportedCount);
    runUpdate.addBindValue(report->archiveImportedCount);
    runUpdate.addBindValue(report->activeImportedCount);
    runUpdate.addBindValue(reportsDir);
    runUpdate.addBindValue(report->summaryText);
    runUpdate.addBindValue(runId);
    if (!runUpdate.exec()) {
        ++report->errorCount;
        qWarning() << "catalog_import_runs update failed" << runUpdate.lastError().text();
    }

        return true;
    });

    if (!tx.ok) {
        if (errorText) *errorText = tx.errorText;
        return false;
    }
    return true;
}
