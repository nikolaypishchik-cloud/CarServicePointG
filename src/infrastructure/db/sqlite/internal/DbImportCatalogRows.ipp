// Extracted from src/infrastructure/db/sqlite/DbImportCatalogFromOrders.cpp for P1/P2 monolith reduction.
// Included exactly once by the original translation unit/header; do not compile separately.

        ++report->conflictCount;
        const CatalogSourceWorkRow sample = agg.sources.isEmpty() ? CatalogSourceWorkRow() : agg.sources.constFirst();
        QStringList idStrings;
        for (int id : candidateIds) idStrings << QString::number(id);
        QSqlQuery cq(d);
        cq.prepare(QStringLiteral("INSERT INTO catalog_import_conflicts(run_id, import_batch_id, source_part_id, order_uid, order_number, source_db_layer, part_number, oem_number, name, conflict_reason, candidate_catalog_ids, recommended_action) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)"));
        cq.addBindValue(runId);
        cq.addBindValue(batchId);
        cq.addBindValue(sample.sourcePartId);
        cq.addBindValue(sample.orderUid);
        cq.addBindValue(sample.orderNumber);
        cq.addBindValue(sample.sourceDbLayer);
        cq.addBindValue(sample.partNumber);
        cq.addBindValue(sample.oem);
        cq.addBindValue(sample.name);
        cq.addBindValue(reason);
        cq.addBindValue(idStrings.join(QStringLiteral("|")));
        cq.addBindValue(recommendedAction);
        if (!cq.exec()) qWarning() << "catalog_import_conflicts insert failed" << cq.lastError().text();

        QJsonObject obj;
        obj.insert(QStringLiteral("source_part_id"), sample.sourcePartId);
        obj.insert(QStringLiteral("order_number"), sample.orderNumber);
        obj.insert(QStringLiteral("source_layer"), sample.sourceDbLayer);
        obj.insert(QStringLiteral("part_number"), sample.partNumber);
        obj.insert(QStringLiteral("oem_number"), sample.oem);
        obj.insert(QStringLiteral("name"), sample.name);
        obj.insert(QStringLiteral("reason"), reason);
        QJsonArray idsArr;
        for (int id : candidateIds) idsArr.append(id);
        obj.insert(QStringLiteral("candidate_catalog_ids"), idsArr);
        obj.insert(QStringLiteral("recommended_action"), recommendedAction);
        conflictJson.append(obj);
        conflictCsv << QStringList({
            QString::number(sample.sourcePartId),
            QString::number(sample.orderNumber),
            sample.sourceDbLayer,
            QStringLiteral("\"") + sample.partNumber + QStringLiteral("\""),
            QStringLiteral("\"") + sample.oem + QStringLiteral("\""),
            QStringLiteral("\"") + sample.name + QStringLiteral("\""),
            QStringLiteral("\"") + reason + QStringLiteral("\""),
            QStringLiteral("\"") + idStrings.join(QStringLiteral("|")) + QStringLiteral("\""),
            QStringLiteral("\"") + recommendedAction + QStringLiteral("\"")
        }).join(QStringLiteral(","));
    };

    auto recordDecisionForRow = [&](const CatalogSourceWorkRow& row,
                                    const QString& decision,
                                    const QString& reasonCode,
                                    const QString& reasonText,
                                    const QString& recommendedAction,
                                    bool canImportAfterRelax,
                                    const QList<int>& candidateCatalogIds,
                                    const QString& dedupeMode,
                                    QVector<CatalogImportDecisionDetail>* target,
                                    QMap<QString, int>* counts,
                                    const QString& candidateClass = QString(),
                                    const QString& sqlOperation = QString(),
                                    const QString& sqlErrorText = QString(),
                                    const QString& bindSnapshotText = QString()) {
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
        detail.decision = decision;
        detail.reasonCode = reasonCode;
        detail.reasonText = reasonText;
        detail.recommendedAction = recommendedAction;
        detail.candidateClass = candidateClass;
        detail.sqlOperation = sqlOperation;
        detail.sqlErrorText = sqlErrorText;
        detail.bindSnapshot = bindSnapshotText;
        detail.canImportAfterRelax = canImportAfterRelax;
        detail.candidateCatalogIds = candidateCatalogIds;
        target->push_back(detail);
        if (counts) (*counts)[reasonCode] += 1;
    };

    auto saveMaps = [&](int catalogId, const CatalogAggregateWorkRow& agg, bool wasCreatedNew) {
        QSqlQuery srcIns(d);
        QSqlQuery mapIns(d);
        QSqlQuery vehIns(d);
        QString safeAggVehicleBrand = agg.vehicleBrand;
        QString safeAggVehicleModel = agg.vehicleModel;
        sanitizeCatalogBrandModel(QString(), &safeAggVehicleBrand, &safeAggVehicleModel);
        vehIns.prepare(QStringLiteral("INSERT OR REPLACE INTO parts_catalog_vehicle_map(catalog_id, vehicle_brand, vehicle_model, year_from, year_to, fuel_type, engine_type, body_type, source_order_uid, last_seen_at) VALUES(?,?,?,?,?,?,?,?,?,?)"));
        vehIns.addBindValue(catalogId);
        vehIns.addBindValue(safeAggVehicleBrand);
        vehIns.addBindValue(safeAggVehicleModel);
        vehIns.addBindValue(agg.yearFrom);
        vehIns.addBindValue(agg.yearTo);
        vehIns.addBindValue(agg.fuelType);
        vehIns.addBindValue(agg.engineType);
        vehIns.addBindValue(agg.bodyType);
        vehIns.addBindValue(agg.sourceOrderUidLast);
        vehIns.addBindValue(agg.lastSeenAt);
        if (!vehIns.exec()) qWarning() << "parts_catalog_vehicle_map upsert failed" << vehIns.lastError().text();

        for (const CatalogSourceWorkRow& row : agg.sources) {
            srcIns.finish();
            srcIns.prepare(QStringLiteral(
                "INSERT INTO parts_catalog_sources(catalog_id, source_part_id, order_uid, order_number, supplier, note, imported_at, source_db_layer, source_signature, import_batch_id, "
                "part_number, oem_number, name, manufacturer, vehicle_brand, vehicle_model, year_from, year_to, fuel_type, engine_type, body_type, group_name, qty, price, amount, purchase_price, purchase_amount, markup_percent, paid, ordered_by_admin, service_id, ordered_at, delivery_date, seen_at) "
                "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
                "ON CONFLICT(catalog_id, source_part_id) DO UPDATE SET order_uid=excluded.order_uid, order_number=excluded.order_number, supplier=excluded.supplier, note=excluded.note, imported_at=excluded.imported_at, source_db_layer=excluded.source_db_layer, source_signature=excluded.source_signature, import_batch_id=excluded.import_batch_id, "
                "part_number=excluded.part_number, oem_number=excluded.oem_number, name=excluded.name, manufacturer=excluded.manufacturer, vehicle_brand=excluded.vehicle_brand, vehicle_model=excluded.vehicle_model, year_from=excluded.year_from, year_to=excluded.year_to, fuel_type=excluded.fuel_type, engine_type=excluded.engine_type, body_type=excluded.body_type, group_name=excluded.group_name, qty=excluded.qty, price=excluded.price, amount=excluded.amount, purchase_price=excluded.purchase_price, purchase_amount=excluded.purchase_amount, markup_percent=excluded.markup_percent, paid=excluded.paid, ordered_by_admin=excluded.ordered_by_admin, service_id=excluded.service_id, ordered_at=excluded.ordered_at, delivery_date=excluded.delivery_date, seen_at=excluded.seen_at"));
            srcIns.addBindValue(catalogId);
            srcIns.addBindValue(row.sourcePartId);
            srcIns.addBindValue(row.orderUid);
            srcIns.addBindValue(row.orderNumber);
            srcIns.addBindValue(row.supplier);
            srcIns.addBindValue(row.note);
            srcIns.addBindValue(nowText);
            srcIns.addBindValue(row.sourceDbLayer);
            srcIns.addBindValue(row.dedupeSignature);
            srcIns.addBindValue(batchId);
            srcIns.addBindValue(row.partNumber);
            srcIns.addBindValue(row.oem);
            srcIns.addBindValue(row.name);
            srcIns.addBindValue(row.manufacturer);
            QString safeRowVehicleBrand = row.vehicleBrand;
            QString safeRowVehicleModel = row.vehicleModel;
            sanitizeCatalogBrandModel(row.rawCar, &safeRowVehicleBrand, &safeRowVehicleModel);
            srcIns.addBindValue(nonNullText(safeRowVehicleBrand));
            srcIns.addBindValue(nonNullText(safeRowVehicleModel));
            srcIns.addBindValue(row.carYear);
            srcIns.addBindValue(row.carYear);
            srcIns.addBindValue(row.fuelType);
            srcIns.addBindValue(row.engineType);
            srcIns.addBindValue(row.bodyType);
            srcIns.addBindValue(row.groupName);
            srcIns.addBindValue(row.qty);
            srcIns.addBindValue(row.price);
            srcIns.addBindValue(row.amount);
            srcIns.addBindValue(row.purchasePrice);
            srcIns.addBindValue(row.purchaseAmount);
            srcIns.addBindValue(row.markupPercent);
            srcIns.addBindValue(row.paid);
            srcIns.addBindValue(row.orderedByAdmin);
            srcIns.addBindValue(row.serviceId);
            srcIns.addBindValue(row.orderedAt);
            srcIns.addBindValue(row.deliveryDate);
            srcIns.addBindValue(row.seenAt);
            if (!srcIns.exec()) qWarning() << "parts_catalog_sources upsert failed" << srcIns.lastError().text();

            mapIns.finish();
            mapIns.prepare(QStringLiteral("INSERT INTO catalog_source_map(source_part_id, source_order_uid, source_order_number, source_db_layer, catalog_item_id, imported_at, import_batch_id, dedupe_mode, was_created_new, was_merged_into_existing, source_signature, source_part_number, source_oem_number, source_name, source_store, source_price, source_qty, source_amount) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(source_signature) DO UPDATE SET catalog_item_id=excluded.catalog_item_id, imported_at=excluded.imported_at, import_batch_id=excluded.import_batch_id, dedupe_mode=excluded.dedupe_mode, was_created_new=excluded.was_created_new, was_merged_into_existing=excluded.was_merged_into_existing, source_part_number=excluded.source_part_number, source_oem_number=excluded.source_oem_number, source_name=excluded.source_name, source_store=excluded.source_store, source_price=excluded.source_price, source_qty=excluded.source_qty, source_amount=excluded.source_amount"));
            mapIns.addBindValue(row.sourcePartId);
            mapIns.addBindValue(row.orderUid);
            mapIns.addBindValue(row.orderNumber);
            mapIns.addBindValue(row.sourceDbLayer);
            mapIns.addBindValue(catalogId);
            mapIns.addBindValue(nowText);
            mapIns.addBindValue(batchId);
            mapIns.addBindValue(agg.dedupeMode);
            mapIns.addBindValue(wasCreatedNew ? 1 : 0);
            mapIns.addBindValue(wasCreatedNew ? 0 : 1);
            mapIns.addBindValue(row.dedupeSignature);
            mapIns.addBindValue(row.partNumber);
            mapIns.addBindValue(row.oem);
            mapIns.addBindValue(row.name);
            mapIns.addBindValue(row.supplier);
            mapIns.addBindValue(row.price);
            mapIns.addBindValue(row.qty);
            mapIns.addBindValue(row.amount);
            if (!mapIns.exec()) qWarning() << "catalog_source_map upsert failed" << mapIns.lastError().text();
            registryBySignature.insert(row.dedupeSignature, catalogId);
        }
    };

    auto schemaAwareInsertCatalog = [&](const CatalogAggregateWorkRow& agg, const QString& importedNote) {
        QStringList cols;
        QStringList placeholders;
        QVariantList binds;
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("vehicle_brand"), agg.vehicleBrand);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("vehicle_model"), agg.vehicleModel);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("year_from"), agg.yearFrom);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("year_to"), agg.yearTo);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("fuel_type"), agg.fuelType);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("engine_type"), agg.engineType);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("body_type"), agg.bodyType);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("group_name"), agg.groupName);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("kind"), agg.kind);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("group_key"), agg.groupKey);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("node_tags"), agg.nodeTags);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("part_number"), agg.partNumber);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("oem_number"), agg.oem);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("normalized_part_number"), agg.normalizedPartNumber);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("normalized_oem_number"), agg.normalizedOemNumber);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("name"), agg.name);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("normalized_name"), agg.normalizedName);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("brand"), agg.brandName);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("supplier"), agg.supplier);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("price"), agg.priceLast);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("min_price"), agg.minPrice);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("max_price"), agg.maxPrice);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("avg_price"), agg.avgPrice);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("last_price"), agg.priceLast);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("currency"), agg.currency.isEmpty() ? QStringLiteral("UAH") : agg.currency);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("unit"), agg.unit.isEmpty() ? QString::fromUtf8("шт") : agg.unit);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("note"), importedNote);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("source_type"), detectCatalogSourceType(agg.sourceDbLayer));
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("source_order_uid_first"), agg.sourceOrderUidFirst);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("source_order_uid_last"), agg.sourceOrderUidLast);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("first_imported_at"), nowText);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("last_imported_at"), nowText);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("manually_edited"), 0);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("is_active"), 1);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("usage_count"), agg.usageCount);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("orders_count"), agg.ordersCount);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("first_seen_at"), agg.firstSeenAt);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("last_seen_at"), agg.lastSeenAt);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("stores_seen"), joinSortedSet(agg.storesSeen));
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("last_store"), agg.lastStore);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("last_order_number"), agg.sourceOrderNumberLast);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("last_order_uid"), agg.sourceOrderUidLast);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("qty_last"), agg.qtyLast);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("amount_last"), agg.amountLast);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("ordered_at"), agg.orderedAt);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("ordered_by_admin"), agg.orderedByAdmin);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("paid"), agg.paid);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("service_id"), agg.serviceId);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("source_part_id"), agg.sources.isEmpty() ? 0 : agg.sources.constLast().sourcePartId);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("source_order_number"), agg.sourceOrderNumberLast);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("source_db_layer"), agg.sourceDbLayer);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("is_from_legacy_import"), agg.isFromLegacyImport ? 1 : 0);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("import_batch_id"), batchId);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("catalog_match_key"), agg.catalogMatchKey);
        appendColumnValue(&cols, &placeholders, &binds, QStringLiteral("dedupe_signature"), agg.dedupeSignature);
        CatalogDbOperationResult result;
        if (cols.isEmpty()) {
            result.errorText = QString::fromUtf8("Schema-aware INSERT не має доступних колонок для запису.");
            return result;
        }
        const QString sql = QStringLiteral("INSERT INTO parts_catalog(") + cols.join(QStringLiteral(",")) + QStringLiteral(") VALUES(") + placeholders.join(QStringLiteral(",")) + QStringLiteral(")");
        QSqlQuery fallbackInsert(d);
        result = execPrepared(fallbackInsert, sql, binds);
        if (result.ok && result.lastInsertId <= 0) {
            QSqlQuery lookup(d);
            lookup.prepare(QStringLiteral("SELECT id FROM parts_catalog WHERE (TRIM(COALESCE(dedupe_signature,''))<>'' AND dedupe_signature=?) OR (TRIM(COALESCE(catalog_match_key,''))<>'' AND catalog_match_key=?) ORDER BY id DESC LIMIT 1"));
            lookup.addBindValue(agg.dedupeSignature);
            lookup.addBindValue(agg.catalogMatchKey);
            if (lookup.exec() && lookup.next()) result.lastInsertId = lookup.value(0).toLongLong();
        }
        return result;
    };

    auto schemaAwareUpdateCatalog = [&](int catalogId,
                                        const QString& mergedVehicleBrand,
                                        const QString& mergedVehicleModel,
                                        int mergedYearFrom,
                                        int mergedYearTo,
                                        const QString& mergedFuel,
                                        const QString& mergedEngine,
                                        const QString& mergedBody,
                                        const QString& mergedGroup,
                                        const QString& mergedPartNumber,
                                        const QString& mergedOem,
                                        const QString& mergedName,
                                        const QString& mergedBrandName,
                                        const QString& mergedSupplier,
                                        const QString& mergedNote,
                                        const QString& sourceFirst,
                                        const QString& sourceLast,
                                        const QString& firstImportedAt,
                                        const ExistingCatalogRecord& rec,
                                        const CatalogAggregateWorkRow& agg) {
        QStringList setters;
        QVariantList binds;
        auto appendSetter = [&](const QString& col, const QVariant& value) {
            if (!partsCatalogColumns.contains(col)) return;
            setters.push_back(col + QStringLiteral("=?"));
            binds.push_back(value);
        };
        appendSetter(QStringLiteral("vehicle_brand"), mergedVehicleBrand);
        appendSetter(QStringLiteral("vehicle_model"), mergedVehicleModel);
        appendSetter(QStringLiteral("year_from"), mergedYearFrom);
        appendSetter(QStringLiteral("year_to"), mergedYearTo);
        appendSetter(QStringLiteral("fuel_type"), mergedFuel);
        appendSetter(QStringLiteral("engine_type"), mergedEngine);
        appendSetter(QStringLiteral("body_type"), mergedBody);
        appendSetter(QStringLiteral("group_name"), mergedGroup);
        appendSetter(QStringLiteral("kind"), agg.kind);
        appendSetter(QStringLiteral("group_key"), agg.groupKey);
        appendSetter(QStringLiteral("node_tags"), agg.nodeTags);
        appendSetter(QStringLiteral("part_number"), mergedPartNumber);
        appendSetter(QStringLiteral("oem_number"), mergedOem);
        appendSetter(QStringLiteral("normalized_part_number"), agg.normalizedPartNumber.isEmpty() ? rec.normalizedPartNumber : agg.normalizedPartNumber);
        appendSetter(QStringLiteral("normalized_oem_number"), agg.normalizedOemNumber.isEmpty() ? rec.normalizedOemNumber : agg.normalizedOemNumber);
        appendSetter(QStringLiteral("name"), mergedName);
        appendSetter(QStringLiteral("normalized_name"), agg.normalizedName.isEmpty() ? rec.normalizedName : agg.normalizedName);
        appendSetter(QStringLiteral("brand"), mergedBrandName);
        appendSetter(QStringLiteral("supplier"), mergedSupplier);
        appendSetter(QStringLiteral("price"), agg.priceLast > 0.0 ? agg.priceLast : rec.price);
        appendSetter(QStringLiteral("min_price"), agg.minPrice > 0.0 ? agg.minPrice : rec.minPrice);
        appendSetter(QStringLiteral("max_price"), agg.maxPrice > 0.0 ? agg.maxPrice : rec.maxPrice);
        appendSetter(QStringLiteral("avg_price"), agg.avgPrice > 0.0 ? agg.avgPrice : rec.avgPrice);
        appendSetter(QStringLiteral("last_price"), agg.priceLast > 0.0 ? agg.priceLast : rec.lastPrice);
        appendSetter(QStringLiteral("note"), mergedNote);
        appendSetter(QStringLiteral("source_type"), detectCatalogSourceType(agg.sourceDbLayer));
        appendSetter(QStringLiteral("source_order_uid_first"), sourceFirst);
        appendSetter(QStringLiteral("source_order_uid_last"), sourceLast);
        appendSetter(QStringLiteral("first_imported_at"), firstImportedAt);
        appendSetter(QStringLiteral("last_imported_at"), nowText);
        appendSetter(QStringLiteral("is_active"), 1);
        appendSetter(QStringLiteral("usage_count"), qMax(rec.usageCount, agg.usageCount));
        appendSetter(QStringLiteral("orders_count"), qMax(rec.ordersCount, agg.ordersCount));
        appendSetter(QStringLiteral("first_seen_at"), rec.firstImportedAt.isEmpty() ? agg.firstSeenAt : rec.firstImportedAt);
        appendSetter(QStringLiteral("last_seen_at"), agg.lastSeenAt.isEmpty() ? nowText : agg.lastSeenAt);
        appendSetter(QStringLiteral("stores_seen"), joinSortedSet(agg.storesSeen));
        appendSetter(QStringLiteral("last_store"), agg.lastStore);
        appendSetter(QStringLiteral("last_order_number"), agg.sourceOrderNumberLast);
        appendSetter(QStringLiteral("last_order_uid"), agg.sourceOrderUidLast);
        appendSetter(QStringLiteral("qty_last"), agg.qtyLast);
        appendSetter(QStringLiteral("amount_last"), agg.amountLast);
        appendSetter(QStringLiteral("ordered_at"), agg.orderedAt);
        appendSetter(QStringLiteral("ordered_by_admin"), agg.orderedByAdmin);
        appendSetter(QStringLiteral("paid"), agg.paid);
        appendSetter(QStringLiteral("service_id"), agg.serviceId);
        appendSetter(QStringLiteral("source_part_id"), agg.sources.isEmpty() ? 0 : agg.sources.constLast().sourcePartId);
        appendSetter(QStringLiteral("source_order_number"), agg.sourceOrderNumberLast);
        appendSetter(QStringLiteral("source_db_layer"), agg.sourceDbLayer);
        appendSetter(QStringLiteral("is_from_legacy_import"), agg.isFromLegacyImport ? 1 : 0);
        appendSetter(QStringLiteral("import_batch_id"), batchId);
        appendSetter(QStringLiteral("catalog_match_key"), agg.catalogMatchKey);
        appendSetter(QStringLiteral("dedupe_signature"), agg.dedupeSignature);
        binds.push_back(catalogId);
        const QString sql = QStringLiteral("UPDATE parts_catalog SET ") + setters.join(QStringLiteral(", ")) + QStringLiteral(" WHERE id=?");
        QSqlQuery fallbackUpdate(d);
        return execPrepared(fallbackUpdate, sql, binds);
    };

    QSqlQuery upd(d), ins(d);
    const auto aggregateKeys = aggregates.keys();
    int linkedSourceCount = 0;
    for (const QString& key : aggregateKeys) {
        const CatalogAggregateWorkRow agg = aggregates.value(key);

        QSet<int> candidateSet;
        QSet<int> mappedIds;
        for (const CatalogSourceWorkRow& row : agg.sources) {
            const int mappedId = registryBySignature.value(row.dedupeSignature, 0);
            if (mappedId > 0 && existingIndexById.contains(mappedId)) mappedIds.insert(existingIndexById.value(mappedId));
        }
        auto appendCandidates = [&](const QList<int>& values) {
            for (int idx : values) candidateSet.insert(idx);
        };
        if (!mappedIds.isEmpty()) candidateSet.unite(mappedIds);
        if (!agg.catalogMatchKey.isEmpty()) appendCandidates(byMatchKey.values(agg.catalogMatchKey));
        if (!agg.normalizedOemNumber.isEmpty()) appendCandidates(byNormOem.values(agg.normalizedOemNumber));
        if (!agg.normalizedPartNumber.isEmpty()) appendCandidates(byNormPart.values(agg.normalizedPartNumber));
        if (!agg.normalizedName.isEmpty() && !normalizedCatalogLookupKey(agg.brandName).isEmpty()) appendCandidates(byBrandName.values(normalizedCatalogLookupKey(agg.brandName) + QStringLiteral("|") + agg.normalizedName));
        if (!agg.normalizedName.isEmpty() && !normalizedCatalogStoreKey(agg.lastStore).isEmpty()) appendCandidates(byNameStore.values(agg.normalizedName + QStringLiteral("|") + normalizedCatalogStoreKey(agg.lastStore)));

        QList<int> candidateIndexes = candidateSet.values();
        std::sort(candidateIndexes.begin(), candidateIndexes.end());
        int chosenExistingIndex = -1;
        QList<int> ambiguousCatalogIds;
        if (!candidateIndexes.isEmpty()) {
            int bestScore = std::numeric_limits<int>::min();
            QList<int> bestIndexes;
            for (int idx : candidateIndexes) {
                const int score = scoreCatalogCandidate(existing[idx], agg, mappedIds, idx);
                if (score > bestScore) {
                    bestScore = score;
                    bestIndexes = { idx };
                } else if (score == bestScore) {
                    bestIndexes.push_back(idx);
                }
            }
            if (bestIndexes.size() == 1 && bestScore > 0) {
                chosenExistingIndex = bestIndexes.constFirst();
            } else if (bestIndexes.size() > 1 && bestScore > 0) {
                for (int idx : bestIndexes) ambiguousCatalogIds << existing[idx].id;
                writeConflict(agg, QString::fromUtf8("ambiguous_catalog_match"), ambiguousCatalogIds, QString::fromUtf8("manual_review_or_create_separate"));
                ++report->skippedCount;
                for (const CatalogSourceWorkRow& row : agg.sources) {
                    recordDecisionForRow(row,
                                         QStringLiteral("conflict"),
                                         QStringLiteral("ambiguous_catalog_match"),
                                         QString::fromUtf8("Знайдено кілька рівноцінних кандидатів каталогу; потрібен manual review."),
                                         QStringLiteral("manual_review_or_create_separate"),
                                         false,
                                         ambiguousCatalogIds,
                                         agg.dedupeMode,
                                         &conflictDetails,
                                         &conflictReasonCounts,
                                         classifyCandidateStrength(agg));
                    recordDecisionForRow(row,
                                         QStringLiteral("skip"),
                                         QStringLiteral("ambiguous_catalog_match"),
                                         QString::fromUtf8("Знайдено кілька рівноцінних кандидатів каталогу; потрібен manual review."),
                                         QStringLiteral("manual_review_or_create_separate"),
                                         false,
                                         ambiguousCatalogIds,
                                         agg.dedupeMode,
                                         &skippedDetails,
                                         &skippedReasonCounts,
                                         classifyCandidateStrength(agg));
                    logImportItem(row, QStringLiteral("conflict"), 0, QString::fromUtf8("Ambiguous catalog match; manual review required."), agg.dedupeMode);
                }
                continue;
            }
        }

        const QString importedNote = buildCatalogImportedNote(agg);
        const CatalogPreflightDecision candidatePreflight = preflightCandidate(agg, chosenExistingIndex < 0);
        if (!candidatePreflight.ok) {
            ++report->skippedCount;
            for (const CatalogSourceWorkRow& row : agg.sources) {
                recordDecisionForRow(row,
                                     QStringLiteral("skip"),
                                     candidatePreflight.reasonCode,
                                     candidatePreflight.reasonText,
                                     QStringLiteral("skip_or_manual_review"),
                                     false,
                                     QList<int>(),
                                     agg.dedupeMode,
                                     &skippedDetails,
                                     &skippedReasonCounts,
                                     candidatePreflight.candidateClass);
                logImportItem(row, QStringLiteral("skip"), 0, candidatePreflight.reasonText, agg.dedupeMode);
            }
            continue;
        }
        if (chosenExistingIndex >= 0 && chosenExistingIndex < existing.size()) {
            ExistingCatalogRecord rec = existing[chosenExistingIndex];
            bool allSourcesAlreadyMappedToChosen = !agg.sources.isEmpty();
            for (const CatalogSourceWorkRow& row : agg.sources) {
                if (registryBySignature.value(row.dedupeSignature, 0) != rec.id) {
                    allSourcesAlreadyMappedToChosen = false;
                    break;
                }
            }
            if (allSourcesAlreadyMappedToChosen && !aggregateAddsUsefulCatalogData(rec, agg)) {
                ++report->skippedCount;
                linkedSourceCount += agg.sources.size();
                saveMaps(rec.id, agg, false);
                const QList<int> candidateIds = { rec.id };
                for (const CatalogSourceWorkRow& row : agg.sources) {
                    recordDecisionForRow(row,
                                         QStringLiteral("skip"),
                                         QStringLiteral("duplicate_no_new_data"),
                                         QString::fromUtf8("Джерельний рядок уже покрито наявною карткою каталогу; нових даних не з'явилося."),
                                         QStringLiteral("already_covered"),
                                         false,
                                         candidateIds,
                                         agg.dedupeMode,
                                         &skippedDetails,
                                         &skippedReasonCounts);
                    logImportItem(row, QStringLiteral("skip"), rec.id, QString::fromUtf8("Already covered by existing catalog card; no new data."), agg.dedupeMode);
                }
                continue;
            }
            const QString mergedVehicleBrand = rec.vehicleBrand.isEmpty() ? agg.vehicleBrand : rec.vehicleBrand;
            const QString mergedVehicleModel = rec.vehicleModel.isEmpty() ? agg.vehicleModel : rec.vehicleModel;
            int mergedYearFrom = rec.yearFrom;
            if (mergedYearFrom <= 0 || (agg.yearFrom > 0 && agg.yearFrom < mergedYearFrom)) mergedYearFrom = agg.yearFrom;
            int mergedYearTo = rec.yearTo;
            if (agg.yearTo > mergedYearTo) mergedYearTo = agg.yearTo;
            const QString mergedFuel = rec.fuelType.isEmpty() ? agg.fuelType : rec.fuelType;
            const QString mergedEngine = rec.engineType.isEmpty() ? agg.engineType : rec.engineType;
            const QString mergedBody = rec.bodyType.isEmpty() ? agg.bodyType : rec.bodyType;
            const QString mergedGroup = rec.groupName.isEmpty() ? agg.groupName : rec.groupName;
            const QString mergedPartNumber = rec.manuallyEdited && !rec.partNumber.isEmpty() ? rec.partNumber : preferredCatalogText(rec.partNumber, agg.partNumber);
            const QString mergedOem = rec.manuallyEdited && !rec.oem.isEmpty() ? rec.oem : preferredCatalogText(rec.oem, agg.oem);
            const QString mergedName = rec.manuallyEdited && !rec.name.isEmpty() ? rec.name : preferredCatalogText(rec.name, agg.name);
            const QString mergedBrandName = rec.manuallyEdited && !rec.brandName.isEmpty() ? rec.brandName : preferredCatalogText(rec.brandName, agg.brandName);
            const QString mergedSupplier = rec.manuallyEdited && !rec.supplier.isEmpty() ? rec.supplier : preferredCatalogText(rec.supplier, agg.supplier);
            const QString mergedNote = mergeUniqueCatalogText(rec.note, importedNote);
            const QString sourceFirst = rec.sourceFirst.isEmpty() ? agg.sourceOrderUidFirst : rec.sourceFirst;
            const QString sourceLast = agg.sourceOrderUidLast.isEmpty() ? rec.sourceLast : agg.sourceOrderUidLast;
            const QString firstImportedAt = rec.firstImportedAt.isEmpty() ? nowText : rec.firstImportedAt;
            CatalogDbOperationResult updateOp = schemaAwareUpdateCatalog(rec.id,
                                                    mergedVehicleBrand,
