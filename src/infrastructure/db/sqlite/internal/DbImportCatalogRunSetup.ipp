// Extracted from src/infrastructure/db/sqlite/DbImportCatalogFromOrders.cpp for P1/P2 monolith reduction.
// Included exactly once by the original translation unit/header; do not compile separately.

bool Database::importCatalogFromOrders(CatalogImportReport* report, QString* errorText)
{
    CatalogImportReport localReport;
    if (!report) report = &localReport;
    *report = CatalogImportReport();

    if (!ensureCatalogImportSchema(errorText)) return false;
    if (!createCatalogMigrationBackup(errorText)) return false;

    QSqlDatabase d = db();
    if (!d.isValid() || (!d.isOpen() && !d.open())) {
        if (errorText) *errorText = d.lastError().text();
        return false;
    }

    const QString batchId = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    const QString reportsDir = QDir(catalogImportReportsRootDir()).filePath(QStringLiteral("execute_") + batchId);
    QDir().mkpath(reportsDir);
    report->importBatchId = batchId;
    report->reportsDir = reportsDir;

    const bool hasActiveParts = tableExistsInDb(d, QStringLiteral("work_order_parts"));
    const bool hasActiveOrders = tableExistsInDb(d, QStringLiteral("work_orders"));
    const bool hasCacheParts = tableExistsInDb(d, QStringLiteral("router_cache_work_order_parts"));
    const bool hasCacheOrders = tableExistsInDb(d, QStringLiteral("router_cache_work_orders"));
    if ((!hasActiveParts || !hasActiveOrders) && (!hasCacheParts || !hasCacheOrders)) {
        if (errorText) *errorText = QString::fromUtf8("Не знайдено джерела запчастин для побудови каталогу.");
        return false;
    }

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("DbImportSupport::importCatalogFromOrders");
    request.entityType = QStringLiteral("catalog");
    request.entityId = batchId;
    request.operation = QStringLiteral("import_catalog_from_orders");
    request.changedTables = {QStringLiteral("catalog_import_runs"), QStringLiteral("catalog_import_items"), QStringLiteral("parts_catalog"), QStringLiteral("parts_catalog_vehicle_map"), QStringLiteral("parts_catalog_sources")};
    request.beginErrorFallback = QString::fromUtf8("Не вдалося почати транзакцію імпорту каталогу.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити транзакцію імпорту каталогу.");

    const auto tx = MutationExecutor::execute(d, request, [&](QSqlDatabase& txDb, QString* txError) {
        QSqlDatabase d = txDb;
        auto fail = [&](const QString& text) {
            if (txError) *txError = text;
            qWarning() << "Catalog import failed:" << text;
            return false;
        };

    QSqlQuery runInsert(d);
    runInsert.prepare(QStringLiteral("INSERT INTO catalog_import_runs(started_at, import_batch_id, reports_dir) VALUES(?,?,?)"));
    const QString nowText = QDateTime::currentDateTime().toString(Qt::ISODate);
    runInsert.addBindValue(nowText);
    runInsert.addBindValue(batchId);
    runInsert.addBindValue(reportsDir);
    if (!runInsert.exec()) return fail(runInsert.lastError().text());
    const int runId = runInsert.lastInsertId().toInt();

    const QSet<QString> partsCatalogColumns = tableColumnsForDb(d, QStringLiteral("parts_catalog"));
    const auto bindSnapshot = [](const QVariantList& values) {
        QStringList lines;
        lines.reserve(values.size());
        for (int i = 0; i < values.size(); ++i) {
            const QVariant v = values.at(i);
            QString rendered;
            if (!v.isValid() || v.isNull()) {
                rendered = QStringLiteral("<NULL>");
            } else if (v.typeId() == QMetaType::QString) {
                rendered = v.toString();
            } else {
                rendered = v.toString();
            }
            lines << QStringLiteral("[%1] %2").arg(i).arg(rendered);
        }
        return lines.join(QStringLiteral(" | "));
    };
    const auto execPrepared = [&](QSqlQuery& query, const QString& sql, const QVariantList& binds) {
        CatalogDbOperationResult result;
        result.sqlText = sql;
        result.bindSnapshot = bindSnapshot(binds);
        query.finish();
        if (!query.prepare(sql)) {
            const QSqlError err = query.lastError();
            result.nativeErrorCode = err.nativeErrorCode();
            result.errorText = QStringLiteral("prepare failed | native=%1 | text=%2")
                                   .arg(result.nativeErrorCode).arg(err.text());
            return result;
        }
        for (const QVariant& v : binds) query.addBindValue(v);
        if (!query.exec()) {
            const QSqlError err = query.lastError();
            result.nativeErrorCode = err.nativeErrorCode();
            result.errorText = QStringLiteral("exec failed | native=%1 | text=%2")
                                   .arg(result.nativeErrorCode).arg(err.text());
            return result;
        }
        result.ok = true;
        result.lastInsertId = query.lastInsertId().toLongLong();
        return result;
    };
    const auto classifyCandidateStrength = [](const CatalogAggregateWorkRow& agg) {
        if (!agg.normalizedOemNumber.isEmpty()) return QStringLiteral("strong_oem");
        if (!agg.normalizedPartNumber.isEmpty()) return QStringLiteral("strong_part_number");
        if (!agg.normalizedName.isEmpty() && !normalizedCatalogBrandKey(agg.brandName).isEmpty()) return QStringLiteral("acceptable_name_brand");
        if (!agg.normalizedName.isEmpty() && (!normalizedCatalogLookupKey(agg.vehicleBrand).isEmpty() || !normalizedCatalogLookupKey(agg.vehicleModel).isEmpty())) return QStringLiteral("acceptable_name_compatibility");
        if (!agg.normalizedName.isEmpty()) return QStringLiteral("weak_name_only");
        return QStringLiteral("invalid_candidate");
    };
    const auto preflightCandidate = [&](const CatalogAggregateWorkRow& agg, bool createMode) {
        CatalogPreflightDecision decision;
        decision.candidateClass = classifyCandidateStrength(agg);
        const QString effectiveName = agg.name.trimmed();
        const QString effectiveCode = !agg.normalizedOemNumber.isEmpty() ? agg.normalizedOemNumber : agg.normalizedPartNumber;
        const bool hasMeaningfulName = !effectiveName.isEmpty() && !isCatalogTrashName(effectiveName);
        if (createMode && effectiveName.isEmpty() && effectiveCode.isEmpty()) {
            decision.reasonCode = QStringLiteral("preflight_missing_identity");
            decision.reasonText = QString::fromUtf8("Кандидат не містить достатньої ідентифікації для створення картки каталогу.");
            return decision;
        }
        if (!hasMeaningfulName && effectiveCode.isEmpty()) {
            decision.reasonCode = QStringLiteral("preflight_trash_name");
            decision.reasonText = QString::fromUtf8("Назва запчастини недостатньо інформативна для надійного create/update.");
            return decision;
        }
        if (decision.candidateClass == QStringLiteral("weak_name_only")
            && !isCatalogMeaningfulNameOnly(agg.normalizedName, agg.brandName, agg.vehicleBrand, agg.vehicleModel, agg.supplier)) {
            decision.reasonCode = QStringLiteral("preflight_weak_name_only");
            decision.reasonText = QString::fromUtf8("Name-only кандидат недостатньо якісний для безпечного create/update без додаткових ознак.");
            return decision;
        }
        if (!partsCatalogColumns.isEmpty() && !partsCatalogColumns.contains(QStringLiteral("part_number"))) {
            decision.reasonCode = QStringLiteral("schema_missing_part_number");
            decision.reasonText = QString::fromUtf8("У схемі каталогу відсутня обов'язкова колонка part_number.");
            return decision;
        }
        if (!partsCatalogColumns.isEmpty() && !partsCatalogColumns.contains(QStringLiteral("name"))) {
            decision.reasonCode = QStringLiteral("schema_missing_name");
            decision.reasonText = QString::fromUtf8("У схемі каталогу відсутня обов'язкова колонка name.");
            return decision;
        }
        decision.ok = true;
        return decision;
    };
    const auto appendColumnValue = [&](QStringList* cols, QStringList* placeholders, QVariantList* binds, const QString& col, const QVariant& value) {
        if (!partsCatalogColumns.contains(col)) return;
        QVariant normalizedValue = value;
        if (normalizedValue.typeId() == QMetaType::QString) {
            normalizedValue = nonNullText(normalizedValue.toString());
        }
        cols->push_back(col);
        placeholders->push_back(QStringLiteral("?"));
        binds->push_back(normalizedValue);
    };

    QStringList sourceSqlParts;
    if (hasActiveParts && hasActiveOrders) {
        sourceSqlParts << QStringLiteral(
            "SELECT p.id AS source_part_id, COALESCE(p.order_number,0) AS order_number, COALESCE(NULLIF(p.order_uid,''), o.order_uid, '') AS order_uid, "
            "'active' AS source_db_layer, COALESCE(o.car,'') AS raw_car, COALESCE(o.car_brand,'') AS vehicle_brand, COALESCE(o.car_model,'') AS vehicle_model, "
            "COALESCE(o.car_year,0) AS car_year, COALESCE(o.fuel_type,'') AS fuel_type, COALESCE(o.engine_type,'') AS engine_type, COALESCE(o.car_kind,'') AS body_type, "
            "COALESCE(p.part_number,'') AS part_number, COALESCE(p.part_number,'') AS oem_number, COALESCE(p.name,'') AS name, COALESCE(p.manufacturer,'') AS manufacturer, COALESCE(p.store,'') AS supplier, COALESCE(p.note,'') AS note, "
            "COALESCE(p.qty,0) AS qty, COALESCE(p.price,0) AS price, COALESCE(p.amount,0) AS amount, COALESCE(p.purchase_price,0) AS purchase_price, COALESCE(p.purchase_amount,0) AS purchase_amount, COALESCE(p.markup_percent,0) AS markup_percent, "
            "COALESCE(p.paid,0) AS paid, COALESCE(p.ordered_by_admin,0) AS ordered_by_admin, COALESCE(p.service_id,0) AS service_id, COALESCE(p.ordered_at,'') AS ordered_at, COALESCE(p.delivery_date,'') AS delivery_date, "
            "COALESCE(NULLIF(substr(o.closed_at,1,10),''), NULLIF(p.ordered_at,''), substr(o.opened_at,1,10), '') AS seen_at, COALESCE(o.status,'') AS order_status "
            "FROM work_order_parts p LEFT JOIN work_orders o ON o.number=p.order_number");
    }
    if (hasCacheParts && hasCacheOrders) {
        sourceSqlParts << QStringLiteral(
            "SELECT p.id AS source_part_id, COALESCE(p.order_number,0) AS order_number, COALESCE(NULLIF(p.order_uid,''), o.order_uid, '') AS order_uid, "
            "'runtime_hydrated' AS source_db_layer, COALESCE(o.car,'') AS raw_car, COALESCE(o.car_brand,'') AS vehicle_brand, COALESCE(o.car_model,'') AS vehicle_model, "
            "COALESCE(o.car_year,0) AS car_year, COALESCE(o.fuel_type,'') AS fuel_type, COALESCE(o.engine_type,'') AS engine_type, COALESCE(o.car_kind,'') AS body_type, "
            "COALESCE(p.part_number,'') AS part_number, COALESCE(p.part_number,'') AS oem_number, COALESCE(p.name,'') AS name, COALESCE(p.manufacturer,'') AS manufacturer, COALESCE(p.store,'') AS supplier, COALESCE(p.note,'') AS note, "
            "COALESCE(p.qty,0) AS qty, COALESCE(p.price,0) AS price, COALESCE(p.amount,0) AS amount, COALESCE(p.purchase_price,0) AS purchase_price, COALESCE(p.purchase_amount,0) AS purchase_amount, COALESCE(p.markup_percent,0) AS markup_percent, "
            "COALESCE(p.paid,0) AS paid, COALESCE(p.ordered_by_admin,0) AS ordered_by_admin, COALESCE(p.service_id,0) AS service_id, COALESCE(p.ordered_at,'') AS ordered_at, COALESCE(p.delivery_date,'') AS delivery_date, "
            "COALESCE(NULLIF(substr(o.closed_at,1,10),''), NULLIF(p.ordered_at,''), substr(o.opened_at,1,10), '') AS seen_at, COALESCE(o.status,'') AS order_status "
            "FROM router_cache_work_order_parts p LEFT JOIN router_cache_work_orders o ON o.number=p.order_number "
            "WHERE NOT EXISTS (SELECT 1 FROM work_orders aw WHERE aw.number=p.order_number)");
    }

    QSqlQuery src(d);
    const QString srcSql = sourceSqlParts.join(QStringLiteral(" UNION ALL ")) + QStringLiteral(" ORDER BY order_number ASC, source_part_id ASC");
    if (!src.exec(srcSql)) return fail(QString::fromUtf8("Не вдалося прочитати джерела запчастин з нарядів.\n") + src.lastError().text());

    QVector<CatalogSourceWorkRow> sourceRows;
    QVector<CatalogSourceWorkRow> skippedInvalidRows;
    QVector<CatalogImportDecisionDetail> createdDetails;
    QVector<CatalogImportDecisionDetail> updatedDetails;
    QVector<CatalogImportDecisionDetail> mergedDetails;
    QVector<CatalogImportDecisionDetail> conflictDetails;
    QVector<CatalogImportDecisionDetail> skippedDetails;
    QVector<CatalogImportDecisionDetail> errorDetails;
    QMap<QString, int> skippedReasonCounts;
    QMap<QString, int> errorReasonCounts;
    QMap<QString, int> conflictReasonCounts;
    QSet<QString> uniqueSourceSignatures;
    int nameOnlyCount = 0;
    int withPartNumberCount = 0;
    int withOemCount = 0;

    while (src.next()) {
        CatalogSourceWorkRow row;
        row.sourcePartId = src.value(0).toInt();
        row.orderNumber = src.value(1).toInt();
        row.orderUid = src.value(2).toString().trimmed();
        row.sourceDbLayer = src.value(3).toString().trimmed();
        row.rawCar = src.value(4).toString().trimmed();
        row.vehicleBrand = src.value(5).toString().trimmed();
        row.vehicleModel = src.value(6).toString().trimmed();
        row.carYear = src.value(7).toInt();
        row.fuelType = src.value(8).toString().trimmed();
        row.engineType = src.value(9).toString().trimmed();
        row.bodyType = src.value(10).toString().trimmed();
        row.partNumber = src.value(11).toString().trimmed();
        row.oem = src.value(12).toString().trimmed();
        row.name = src.value(13).toString().trimmed();
        row.manufacturer = src.value(14).toString().trimmed();
        row.supplier = src.value(15).toString().trimmed();
        row.note = src.value(16).toString().trimmed();
        row.qty = src.value(17).toDouble();
        row.price = src.value(18).toDouble();
        row.amount = src.value(19).toDouble();
        row.purchasePrice = src.value(20).toDouble();
        row.purchaseAmount = src.value(21).toDouble();
        row.markupPercent = src.value(22).toDouble();
        row.paid = src.value(23).toInt();
        row.orderedByAdmin = src.value(24).toInt();
        row.serviceId = src.value(25).toInt();
        row.orderedAt = src.value(26).toString().trimmed();
        row.deliveryDate = src.value(27).toString().trimmed();
        row.seenAt = src.value(28).toString().trimmed();
        row.currency = QStringLiteral("UAH");
        row.unit = QString::fromUtf8("шт");
        row.brandName = row.manufacturer.trimmed();
        if (row.vehicleBrand.isEmpty() && row.vehicleModel.isEmpty()) {
            splitCatalogBrandModelFallback(row.rawCar, &row.vehicleBrand, &row.vehicleModel);
        }
        sanitizeCatalogBrandModel(row.rawCar, &row.vehicleBrand, &row.vehicleModel);
        row.normalizedName = normalizedCatalogLookupKey(row.name);
        row.normalizedPartNumber = normalizedCatalogPartCode(row.partNumber);
        row.normalizedOemNumber = normalizedCatalogPartCode(row.oem.isEmpty() ? row.partNumber : row.oem);
        row.normalizedStore = normalizedCatalogStoreKey(row.supplier);
        row.groupName = businessGroupFromLegacyFields(QString(), row.kind, row.groupKey, row.name, row.note);
        QString dedupeMode;
        row.catalogMatchKey = buildCatalogMatchKey(row, &dedupeMode);
        row.dedupeSignature = buildCatalogSourceSignature(row);
        row.fromLegacyLike = row.sourceDbLayer != QStringLiteral("active");

        ++report->processedCount;
        const CatalogEligibilityResult eligibility = classifyCatalogSourceEligibility(row);
        if (!eligibility.eligible) {
            ++report->skippedCount;
            skippedInvalidRows.push_back(row);
            CatalogImportDecisionDetail detail;
            detail.sourcePartId = row.sourcePartId;
            detail.orderNumber = row.orderNumber;
            detail.orderUid = row.orderUid;
            detail.sourceDbLayer = row.sourceDbLayer;
            detail.partNumber = row.partNumber;
            detail.oemNumber = row.oem;
            detail.name = row.name;
            detail.supplier = row.supplier;
            detail.note = row.note;
            detail.normalizedPartNumber = row.normalizedPartNumber;
            detail.normalizedOemNumber = row.normalizedOemNumber;
            detail.normalizedName = row.normalizedName;
            detail.normalizedStore = row.normalizedStore;
            detail.dedupeMode = dedupeMode;
            detail.catalogMatchKey = row.catalogMatchKey;
            detail.sourceSignature = row.dedupeSignature;
            detail.decision = QStringLiteral("skip");
            detail.reasonCode = eligibility.reasonCode;
            detail.reasonText = eligibility.reasonText;
            detail.recommendedAction = eligibility.recommendedAction;
            detail.candidateClass = row.normalizedOemNumber.isEmpty() && row.normalizedPartNumber.isEmpty() && !row.normalizedName.isEmpty()
                ? QStringLiteral("weak_name_only")
                : QStringLiteral("invalid_candidate");
            detail.canImportAfterRelax = eligibility.canImportAfterRelax;
            skippedDetails.push_back(detail);
            skippedReasonCounts[detail.reasonCode] += 1;
            continue;
        }
        ++report->eligibleCount;
        if (row.normalizedPartNumber.isEmpty() && !row.normalizedName.isEmpty()) ++nameOnlyCount;
        if (!row.normalizedPartNumber.isEmpty()) ++withPartNumberCount;
        if (!row.normalizedOemNumber.isEmpty()) ++withOemCount;
        if (row.sourceDbLayer == QStringLiteral("active")) ++report->activeImportedCount; else ++report->legacyImportedCount;
        uniqueSourceSignatures.insert(row.dedupeSignature);
        sourceRows.push_back(row);
    }
    report->uniqueSourceCount = uniqueSourceSignatures.size();
    report->sourceLayerCount = (hasActiveParts && hasActiveOrders ? 1 : 0) + (hasCacheParts && hasCacheOrders ? 1 : 0);

    QMap<QString, CatalogAggregateWorkRow> aggregates;
    for (const CatalogSourceWorkRow& row : sourceRows) {
        QString key = row.catalogMatchKey;
        if (key.trimmed().isEmpty()) key = QStringLiteral("sig:") + row.dedupeSignature;
        CatalogAggregateWorkRow& agg = aggregates[key];
        agg.sources.push_back(row);
        if (agg.catalogMatchKey.isEmpty()) agg.catalogMatchKey = key;
        if (agg.dedupeMode.isEmpty()) {
            QString mode;
            buildCatalogMatchKey(row, &mode);
            agg.dedupeMode = mode;
        }
        agg.vehicleBrand = preferredCatalogText(agg.vehicleBrand, row.vehicleBrand);
        agg.vehicleModel = preferredCatalogText(agg.vehicleModel, row.vehicleModel);
        agg.fuelType = preferredCatalogText(agg.fuelType, row.fuelType);
        agg.engineType = preferredCatalogText(agg.engineType, row.engineType);
        agg.bodyType = preferredCatalogText(agg.bodyType, row.bodyType);
        agg.partNumber = preferredCatalogText(agg.partNumber, row.partNumber);
        agg.oem = preferredCatalogText(agg.oem, row.oem.isEmpty() ? row.partNumber : row.oem);
        agg.normalizedPartNumber = preferredCatalogText(agg.normalizedPartNumber, row.normalizedPartNumber);
        agg.normalizedOemNumber = preferredCatalogText(agg.normalizedOemNumber, row.normalizedOemNumber);
        agg.name = preferredCatalogText(agg.name, row.name);
        agg.normalizedName = preferredCatalogText(agg.normalizedName, row.normalizedName);
        agg.brandName = preferredCatalogText(agg.brandName, row.brandName);
        agg.manufacturer = preferredCatalogText(agg.manufacturer, row.manufacturer);
        agg.supplier = preferredCatalogText(agg.supplier, row.supplier);
        agg.note = mergeUniqueCatalogText(agg.note, row.note);
        agg.unit = preferredCatalogText(agg.unit, row.unit);
        agg.currency = preferredCatalogText(agg.currency, row.currency);
        agg.kind = preferredCatalogText(agg.kind, row.kind);
        agg.groupKey = preferredCatalogText(agg.groupKey, row.groupKey);
        agg.groupName = preferredCatalogText(agg.groupName, row.groupName);
        agg.nodeTags = mergeUniqueCatalogText(agg.nodeTags, row.nodeTags);
        agg.usageCount += 1;
        const QString orderIdentity = !row.orderUid.isEmpty() ? row.orderUid : QString::number(row.orderNumber);
        if (!orderIdentity.isEmpty()) agg.seenOrders.insert(orderIdentity);
        if (!row.supplier.trimmed().isEmpty()) agg.storesSeen.insert(row.supplier.trimmed());
        if (row.carYear > 0) {
            if (agg.yearFrom <= 0 || row.carYear < agg.yearFrom) agg.yearFrom = row.carYear;
            if (row.carYear > agg.yearTo) agg.yearTo = row.carYear;
        }
        if (row.price > 0.0) {
            if (agg.minPrice <= 0.0 || row.price < agg.minPrice) agg.minPrice = row.price;
            if (row.price > agg.maxPrice) agg.maxPrice = row.price;
            agg.priceSum += row.price;
            agg.priceCount += 1;
        }
        const QString seenAt = normalizedLooseIsoDate(row.seenAt);
        if (!seenAt.isEmpty()) {
            if (agg.firstSeenAt.isEmpty() || seenAt < agg.firstSeenAt) {
                agg.firstSeenAt = seenAt;
                agg.sourceOrderUidFirst = row.orderUid;
                agg.sourceOrderNumberFirst = row.orderNumber;
            }
            if (agg.lastSeenAt.isEmpty() || seenAt >= agg.lastSeenAt) {
                agg.lastSeenAt = seenAt;
                agg.sourceOrderUidLast = row.orderUid;
                agg.sourceOrderNumberLast = row.orderNumber;
                agg.lastStore = row.supplier;
                agg.orderedAt = row.orderedAt;
                agg.qtyLast = row.qty;
                agg.priceLast = row.price;
                agg.amountLast = row.amount;
                agg.orderedByAdmin = row.orderedByAdmin;
                agg.paid = row.paid;
                agg.serviceId = row.serviceId;
                agg.sourceDbLayer = row.sourceDbLayer;
            }
        } else if (agg.lastSeenAt.isEmpty()) {
            agg.sourceOrderUidLast = row.orderUid;
            agg.sourceOrderNumberLast = row.orderNumber;
            agg.lastStore = row.supplier;
            agg.orderedAt = row.orderedAt;
            agg.qtyLast = row.qty;
            agg.priceLast = row.price;
            agg.amountLast = row.amount;
            agg.orderedByAdmin = row.orderedByAdmin;
            agg.paid = row.paid;
            agg.serviceId = row.serviceId;
            agg.sourceDbLayer = row.sourceDbLayer;
        }
        agg.dedupeSignature = preferredCatalogText(agg.dedupeSignature, row.dedupeSignature);
        if (row.fromLegacyLike) agg.isFromLegacyImport = true;
    }

    for (auto it = aggregates.begin(); it != aggregates.end(); ++it) {
        CatalogAggregateWorkRow& agg = it.value();
        agg.ordersCount = agg.seenOrders.size();
        if (agg.priceCount > 0) agg.avgPrice = agg.priceSum / double(agg.priceCount);
        if (agg.lastSeenAt.isEmpty()) agg.lastSeenAt = normalizedLooseIsoDate(agg.orderedAt);
        if (agg.firstSeenAt.isEmpty()) agg.firstSeenAt = agg.lastSeenAt;
        if (agg.sourceDbLayer.isEmpty()) agg.sourceDbLayer = agg.isFromLegacyImport ? QStringLiteral("runtime_hydrated") : QStringLiteral("active");
        if (agg.lastStore.isEmpty()) agg.lastStore = agg.supplier;
        if (agg.groupName.isEmpty()) agg.groupName = inferCatalogGroupFromText(agg.name, agg.note);
        if (agg.partNumber.isEmpty()) agg.partNumber = agg.oem;
        if (agg.oem.isEmpty()) agg.oem = agg.partNumber;
    }

    QVector<ExistingCatalogRecord> existing;
    QMap<int, int> existingIndexById;
    QMultiMap<QString, int> byMatchKey;
    QMultiMap<QString, int> byNormOem;
    QMultiMap<QString, int> byNormPart;
    QMultiMap<QString, int> byBrandName;
    QMultiMap<QString, int> byNameStore;
    QHash<QString, int> registryBySignature;

    QSqlQuery existingQ(d);
    if (!existingQ.exec(QStringLiteral(
            "SELECT id, COALESCE(part_number,''), COALESCE(oem_number,''), COALESCE(normalized_part_number,''), COALESCE(normalized_oem_number,''), COALESCE(name,''), COALESCE(normalized_name,''), COALESCE(brand,''), COALESCE(supplier,''), COALESCE(catalog_match_key,''), COALESCE(dedupe_signature,''), COALESCE(vehicle_brand,''), COALESCE(vehicle_model,''), COALESCE(year_from,0), COALESCE(year_to,0), COALESCE(fuel_type,''), COALESCE(engine_type,''), COALESCE(body_type,''), COALESCE(group_name,''), COALESCE(note,''), COALESCE(source_order_uid_first,''), COALESCE(source_order_uid_last,''), COALESCE(first_imported_at,''), COALESCE(last_imported_at,''), COALESCE(price,0), COALESCE(min_price,0), COALESCE(max_price,0), COALESCE(avg_price,0), COALESCE(last_price,0), COALESCE(qty_last,0), COALESCE(amount_last,0), COALESCE(usage_count,0), COALESCE(orders_count,0), COALESCE(last_store,''), COALESCE(last_order_number,0), COALESCE(last_order_uid,''), COALESCE(source_db_layer,''), COALESCE(manually_edited,0) "
            "FROM parts_catalog WHERE COALESCE(is_active,1)=1"))) {
        return fail(QString::fromUtf8("Не вдалося прочитати існуючий каталог.\n") + existingQ.lastError().text());
    }
    while (existingQ.next()) {
        ExistingCatalogRecord rec;
        rec.id = existingQ.value(0).toInt();
        rec.partNumber = existingQ.value(1).toString().trimmed();
        rec.oem = existingQ.value(2).toString().trimmed();
        rec.normalizedPartNumber = preferredCatalogText(existingQ.value(3).toString().trimmed(), normalizedCatalogPartCode(rec.partNumber));
        rec.normalizedOemNumber = preferredCatalogText(existingQ.value(4).toString().trimmed(), normalizedCatalogPartCode(rec.oem.isEmpty() ? rec.partNumber : rec.oem));
        rec.name = existingQ.value(5).toString().trimmed();
        rec.normalizedName = preferredCatalogText(existingQ.value(6).toString().trimmed(), normalizedCatalogLookupKey(rec.name));
        rec.brandName = existingQ.value(7).toString().trimmed();
        rec.supplier = existingQ.value(8).toString().trimmed();
        rec.normalizedStore = normalizedCatalogStoreKey(rec.supplier);
        rec.catalogMatchKey = existingQ.value(9).toString().trimmed();
        rec.dedupeSignature = existingQ.value(10).toString().trimmed();
        rec.vehicleBrand = existingQ.value(11).toString().trimmed();
        rec.vehicleModel = existingQ.value(12).toString().trimmed();
        rec.yearFrom = existingQ.value(13).toInt();
        rec.yearTo = existingQ.value(14).toInt();
        rec.fuelType = existingQ.value(15).toString().trimmed();
        rec.engineType = existingQ.value(16).toString().trimmed();
        rec.bodyType = existingQ.value(17).toString().trimmed();
        rec.groupName = existingQ.value(18).toString().trimmed();
        rec.note = existingQ.value(19).toString();
        rec.sourceFirst = existingQ.value(20).toString().trimmed();
        rec.sourceLast = existingQ.value(21).toString().trimmed();
        rec.firstImportedAt = existingQ.value(22).toString().trimmed();
        rec.lastImportedAt = existingQ.value(23).toString().trimmed();
        rec.price = existingQ.value(24).toDouble();
        rec.minPrice = existingQ.value(25).toDouble();
        rec.maxPrice = existingQ.value(26).toDouble();
        rec.avgPrice = existingQ.value(27).toDouble();
        rec.lastPrice = existingQ.value(28).toDouble();
        rec.qtyLast = existingQ.value(29).toDouble();
        rec.amountLast = existingQ.value(30).toDouble();
        rec.usageCount = existingQ.value(31).toInt();
        rec.ordersCount = existingQ.value(32).toInt();
        rec.lastStore = existingQ.value(33).toString().trimmed();
        rec.lastOrderNumber = existingQ.value(34).toInt();
        rec.lastOrderUid = existingQ.value(35).toString().trimmed();
        rec.sourceDbLayer = existingQ.value(36).toString().trimmed();
        rec.manuallyEdited = existingQ.value(37).toInt() != 0;
        existingIndexById.insert(rec.id, existing.size());
        if (!rec.catalogMatchKey.isEmpty()) byMatchKey.insert(rec.catalogMatchKey, existing.size());
        if (!rec.normalizedOemNumber.isEmpty()) byNormOem.insert(rec.normalizedOemNumber, existing.size());
        if (!rec.normalizedPartNumber.isEmpty()) byNormPart.insert(rec.normalizedPartNumber, existing.size());
        if (!rec.normalizedName.isEmpty() && !normalizedCatalogLookupKey(rec.brandName).isEmpty()) byBrandName.insert(normalizedCatalogLookupKey(rec.brandName) + QStringLiteral("|") + rec.normalizedName, existing.size());
        if (!rec.normalizedName.isEmpty()) byNameStore.insert(rec.normalizedName + QStringLiteral("|") + rec.normalizedStore, existing.size());
        existing.push_back(rec);
    }

    QSqlQuery registryRead(d);
    if (registryRead.exec(QStringLiteral("SELECT COALESCE(source_signature,''), COALESCE(catalog_item_id,0) FROM catalog_source_map WHERE TRIM(COALESCE(source_signature,''))<>''"))) {
        while (registryRead.next()) registryBySignature.insert(registryRead.value(0).toString(), registryRead.value(1).toInt());
    }

    QSqlQuery logItem(d);
    auto logImportItem = [&](const CatalogSourceWorkRow& row,
                             const QString& action,
                             int catalogId,
                             const QString& message,
                             const QString& dedupeMode) {
        logItem.finish();
        logItem.prepare(QStringLiteral("INSERT INTO catalog_import_items(run_id, source_part_id, order_uid, order_number, part_number, name, action, catalog_id, message, is_oil, source_db_layer, import_batch_id, dedupe_mode, source_signature) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
        logItem.addBindValue(runId);
        logItem.addBindValue(row.sourcePartId);
        logItem.addBindValue(row.orderUid);
        logItem.addBindValue(row.orderNumber);
        logItem.addBindValue(row.partNumber.isEmpty() ? row.oem : row.partNumber);
        logItem.addBindValue(row.name);
        logItem.addBindValue(action);
        logItem.addBindValue(catalogId);
        logItem.addBindValue(message);
        logItem.addBindValue(0);
        logItem.addBindValue(row.sourceDbLayer);
        logItem.addBindValue(batchId);
        logItem.addBindValue(dedupeMode);
        logItem.addBindValue(row.dedupeSignature);
        if (!logItem.exec()) qWarning() << "catalog_import_items insert failed" << logItem.lastError().text();
    };

    QJsonArray conflictJson;
    QStringList conflictCsv;
    conflictCsv << QStringLiteral("source_part_id,order_number,source_layer,part_number,oem_number,name,reason,candidate_catalog_ids,recommended_action");

    auto writeConflict = [&](const CatalogAggregateWorkRow& agg, const QString& reason, const QList<int>& candidateIds, const QString& recommendedAction) {
