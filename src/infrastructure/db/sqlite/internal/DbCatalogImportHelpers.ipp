// Extracted from src/infrastructure/db/sqlite/DbCatalogImportLocalSupport.h for P2 prelude monolith reduction.
// Included exactly once by the owning header; do not compile separately.

             row.normalizedOemNumber,
             row.normalizedName,
             row.normalizedStore,
             catalogImportPriceBucket(row.price),
             QString::number(row.serviceId));
    return QString::fromLatin1(QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Sha1).toHex());
}

static bool isCatalogTrashName(const QString& value)
{
    const QString folded = foldedCatalogValue(value);
    if (folded.isEmpty()) return true;
    const QString compact = normalizedCatalogLookupKey(folded);
    if (compact.size() < 3) return true;
    static const QSet<QString> trash = {
        QString::fromUtf8("запчасть"),
        QString::fromUtf8("запчастина"),
        QString::fromUtf8("запчастини"),
        QString::fromUtf8("деталь"),
        QString::fromUtf8("часть"),
        QStringLiteral("part"),
        QStringLiteral("parts")
    };
    return trash.contains(folded) || trash.contains(compact);
}

static CatalogEligibilityResult classifyCatalogSourceEligibility(const CatalogSourceWorkRow& row)
{
    CatalogEligibilityResult result;
    result.recommendedAction = QStringLiteral("skip");

    if (!row.normalizedOemNumber.isEmpty() || !row.normalizedPartNumber.isEmpty()) {
        result.eligible = true;
        result.reasonCode = QStringLiteral("ok_code_or_oem");
        result.reasonText = QString::fromUtf8("Запис має валідний OEM/part number.");
        result.recommendedAction = QStringLiteral("import");
        return result;
    }
    if (!row.normalizedName.isEmpty() && isCatalogTrashName(row.name)) {
        result.reasonCode = QStringLiteral("trash_name");
        result.reasonText = QString::fromUtf8("Назва запчастини занадто загальна або сміттєва.");
        result.recommendedAction = QStringLiteral("skip");
        return result;
    }
    if (!row.normalizedName.isEmpty() && isCatalogMeaningfulNameOnly(row.normalizedName,
                                                                      row.brandName,
                                                                      row.vehicleBrand,
                                                                      row.vehicleModel,
                                                                      row.supplier)) {
        result.eligible = true;
        result.reasonCode = QStringLiteral("ok_name_only");
        result.reasonText = QString::fromUtf8("Name-only запис придатний до імпорту після нормалізації.");
        result.recommendedAction = QStringLiteral("import");
        return result;
    }
    if (!row.normalizedName.isEmpty()) {
        result.reasonCode = QStringLiteral("weak_name_only");
        result.reasonText = QString::fromUtf8("Назви недостатньо для надійного створення картки без додаткових ознак.");
        result.canImportAfterRelax = true;
        result.recommendedAction = QStringLiteral("manual_review_or_skip");
        return result;
    }
    if (!row.normalizedStore.isEmpty() || row.price > 0.0 || row.qty > 0.0 || row.amount > 0.0) {
        result.reasonCode = QStringLiteral("insufficient_identification");
        result.reasonText = QString::fromUtf8("Недостатньо ідентифікуючих даних для створення осмисленої картки каталогу.");
        result.canImportAfterRelax = false;
        result.recommendedAction = QStringLiteral("skip_or_manual_review");
        return result;
    }

    result.reasonCode = QStringLiteral("empty_source_row");
    result.reasonText = QString::fromUtf8("Рядок не містить корисних даних для імпорту.");
    return result;
}

static QString catalogImportReportsRootDir()
{
    const QString dir = QDir(AppRuntime::logsDir()).filePath(QStringLiteral("catalog_import_reports"));
    QDir().mkpath(dir);
    return dir;
}

static bool writeUtf8TextFile(const QString& path, const QString& text)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts << text;
    return true;
}

static bool createCatalogMigrationBackup(QString* errorText)
{
    const QString src = AppRuntime::activeDbFilePath();
    if (src.trimmed().isEmpty() || !QFileInfo::exists(src)) return true;
    const QFileInfo fi(src);
    const QString backupDir = fi.absolutePath() + QStringLiteral("/catalog_schema_backups");
    QDir().mkpath(backupDir);
    const QString dst = backupDir + QStringLiteral("/service_center_catalog_backup_") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")) + QStringLiteral(".db");
    if (QFile::exists(dst)) return true;
    if (!QFile::copy(src, dst)) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося створити backup БД перед міграцією каталогу.");
        return false;
    }
    return true;
}

static QString joinSortedSet(const QSet<QString>& values)
{
    QStringList list = values.values();
    list.removeAll(QString());
    std::sort(list.begin(), list.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return list.join(QStringLiteral("; "));
}

static QString buildCatalogImportedNote(const CatalogAggregateWorkRow& agg)
{
    QString note = nonNullText(agg.note).trimmed();
    QString srcText;
    if (!agg.sourceOrderUidLast.trimmed().isEmpty()) {
        srcText = QString::fromUtf8("Джерело імпорту: наряд ") + agg.sourceOrderUidLast.trimmed();
    } else if (agg.sourceOrderNumberLast > 0) {
        srcText = QString::fromUtf8("Джерело імпорту: наряд #") + QString::number(agg.sourceOrderNumberLast);
    }
    if (!srcText.isEmpty()) note = mergeUniqueCatalogText(note, srcText);
    if (!agg.lastStore.trimmed().isEmpty()) note = mergeUniqueCatalogText(note, QString::fromUtf8("Останній магазин: ") + agg.lastStore.trimmed());
    return nonNullText(note).trimmed();
}

static bool aggregateAddsUsefulCatalogData(const ExistingCatalogRecord& rec, const CatalogAggregateWorkRow& agg)
{
    auto addsText = [](const QString& existingValue, const QString& incomingValue) {
        return existingValue.trimmed().isEmpty() && !incomingValue.trimmed().isEmpty();
    };
    if (addsText(rec.partNumber, agg.partNumber)) return true;
    if (addsText(rec.oem, agg.oem)) return true;
    if (addsText(rec.name, agg.name)) return true;
    if (addsText(rec.brandName, agg.brandName)) return true;
    if (addsText(rec.supplier, agg.supplier)) return true;
    if (addsText(rec.vehicleBrand, agg.vehicleBrand)) return true;
    if (addsText(rec.vehicleModel, agg.vehicleModel)) return true;
    if (addsText(rec.fuelType, agg.fuelType)) return true;
    if (addsText(rec.engineType, agg.engineType)) return true;
    if (addsText(rec.bodyType, agg.bodyType)) return true;
    if (addsText(rec.groupName, agg.groupName)) return true;
    if (rec.yearFrom <= 0 && agg.yearFrom > 0) return true;
    if (agg.yearTo > rec.yearTo) return true;
    if (rec.minPrice <= 0.0 && agg.minPrice > 0.0) return true;
    if (agg.minPrice > 0.0 && (rec.minPrice <= 0.0 || agg.minPrice < rec.minPrice)) return true;
    if (agg.maxPrice > rec.maxPrice) return true;
    if (rec.lastPrice <= 0.0 && agg.priceLast > 0.0) return true;
    if (rec.lastOrderNumber <= 0 && agg.sourceOrderNumberLast > 0) return true;
    if (rec.lastOrderUid.trimmed().isEmpty() && !agg.sourceOrderUidLast.trimmed().isEmpty()) return true;
    return false;
}

static int scoreCatalogCandidate(const ExistingCatalogRecord& rec,
                                 const CatalogAggregateWorkRow& agg,
                                 const QSet<int>& mappedCandidateIndexes,
                                 int candidateIndex)
{
    int score = 0;
    if (mappedCandidateIndexes.contains(candidateIndex)) score += 1000;
    if (!agg.catalogMatchKey.isEmpty() && rec.catalogMatchKey == agg.catalogMatchKey) score += 450;
    if (!agg.normalizedOemNumber.isEmpty() && rec.normalizedOemNumber == agg.normalizedOemNumber) score += 400;
    if (!agg.normalizedPartNumber.isEmpty() && rec.normalizedPartNumber == agg.normalizedPartNumber) score += 350;
    const QString recBrand = normalizedCatalogLookupKey(rec.brandName);
    const QString aggBrand = normalizedCatalogLookupKey(agg.brandName);
    if (!agg.normalizedName.isEmpty() && rec.normalizedName == agg.normalizedName) score += 120;
    if (!agg.normalizedName.isEmpty() && !aggBrand.isEmpty() && rec.normalizedName == agg.normalizedName && recBrand == aggBrand) score += 160;
    if (!agg.lastStore.isEmpty() && rec.normalizedStore == normalizedCatalogStoreKey(agg.lastStore)) score += 60;
    if (!agg.vehicleBrand.isEmpty() && foldedCatalogValue(rec.vehicleBrand) == foldedCatalogValue(agg.vehicleBrand)) score += 30;
    if (!agg.vehicleModel.isEmpty() && foldedCatalogValue(rec.vehicleModel) == foldedCatalogValue(agg.vehicleModel)) score += 20;
    if (!agg.groupName.isEmpty() && foldedCatalogValue(rec.groupName) == foldedCatalogValue(agg.groupName)) score += 10;
    return score;
}

static QString detectCatalogSourceType(const QString& layer)
{
    if (layer == QStringLiteral("active")) return QStringLiteral("active");
    if (layer == QStringLiteral("runtime_hydrated")) return QStringLiteral("runtime_hydrated");
    if (layer == QStringLiteral("archive")) return QStringLiteral("archive");
    if (layer == QStringLiteral("legacy")) return QStringLiteral("legacy");
    return layer.trimmed().isEmpty() ? QStringLiteral("orders_import") : layer.trimmed();
}


} // namespace
