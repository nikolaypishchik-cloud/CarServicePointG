#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"
#include "core/services/orders/PartPricingEngine.h"
#include "core/services/orders/PartMarkupPolicyService.h"

class RegressionMarkupRecalculationTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
    void importedPurchasePriceGetsPolicyMarkup();
    void manualInlineClientPriceBackCalculatesPurchasePrice();
    void manualOverrideHasPriorityOverPolicy();
    void manualOverrideZeroMeansPurchaseEqualsClient();
    void globalPolicyRecalcMustPreserveManualOverrideContract();
    void sameInputCannotBeMarkedUpTwiceAfterReopen();
    void emptyStoreIgnoresGlobalPartsMarkupFallback();
    void motorOilWithEmptyStoreIgnoresGlobalFallbacks();
    void zeroOilRuleFallsBackOnlyToConfiguredStoreMarkup();
    void manualEditorPriceIsPersistedAsPurchaseBase();
    void importedRowsUseOrderedDateAsOverdueAnchor();
    void legacyRepairAndDbContextContractsRemainWired();
    void storeAliasesUseOneCanonicalPolicyKey();
    void reportsFallbackDoesNotTreatOilRuleAsKnownPartStore();
    void reportsDoNotKeepParallelStoreLookupOrGlobalDefaultReads();
    void disconnectedSupportAndCacheInvalidationAreRejected();
};

static QString connectedDialogSupport()
{
    const auto entry = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsDialogSupport.cpp");
    QString result = entry;
    for (const char* name : {"WorkPartsDialogBackupAndImport.ipp", "WorkPartsDialogRowState.ipp", "WorkPartsDialogPersistence.ipp"}) {
        const auto path = QStringLiteral("ui/tabs/orders/internal/") + QString::fromLatin1(name);
        if (!cs::tests::sourceIncludesDirective(entry, path)) {
            QTest::qFail(qPrintable("Missing support include: " + path), __FILE__, __LINE__);
            return {};
        }
        result += cs::tests::readSourceFile((QStringLiteral("src/") + path).toUtf8().constData());
    }
    return result;
}

static bool reportsInvalidationConnected(const QString& postCommit, const QString& invalidator)
{
    const auto start = invalidator.indexOf(QStringLiteral("void invalidateOrdersAndParts(const QString& reason)"));
    const auto end = invalidator.indexOf(QStringLiteral("void invalidateAfterMutation("), start);
    const auto reportStart = invalidator.indexOf(QStringLiteral("void invalidateReportsAndStats(const QString& reason)"));
    if (reportStart < 0 || start <= reportStart || end <= start) return false;
    return postCommit.contains(QStringLiteral("SnapshotCacheInvalidator::invalidateOrdersAndParts("))
        && invalidator.mid(start, end - start).contains(QStringLiteral("invalidateReportsAndStats(reason);"))
        && invalidator.mid(reportStart, start - reportStart).contains(QStringLiteral("ReportsSnapshotCache::invalidateReports();"));
}

static PartMarkupPolicyService::PolicySnapshot policyWith(const QString& store, double pct)
{
    PartMarkupPolicyService::PolicySnapshot snapshot;
    snapshot.storeMarkups.insert(store, pct);
    return snapshot;
}

void RegressionMarkupRecalculationTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireNotContains("src/core/services/orders/PartPricingEngine.cpp", "calculateWithPolicyPercent");
    cs::tests::requireContains("src/core/services/orders/PartPricingEngine.cpp", "manualMarkupOverride");
    cs::tests::requireContains("src/core/services/orders/PartPricingEngine.cpp", "PART_PRICING op=calculate");
    cs::tests::requireContains("src/core/repository/PartRepository.cpp", "manual_markup_override");
    cs::tests::requireNotContains("src/core/repository/PartRepository.cpp", "price_input_mode");
    cs::tests::requireContains("src/core/repository/PartRepository.cpp", "pricing_calc_version");
    cs::tests::requireContains("src/ui/tabs/orders/WorkPartsPartPersistence.cpp", "payloadInput.manualMarkupOverride");
    cs::tests::requireNotContains("src/ui/tabs/orders/WorkPartsPartPersistence.cpp", "payloadInput.priceInputMode");
    cs::tests::requireContains("src/ui/tabs/orders/WorkPartsCrudController.cpp", "Закупівельна ціна");
    cs::tests::requireContains("src/ui/tabs/orders/WorkPartsCrudController.cpp", "Націнка %");
    cs::tests::requireContains("src/ui/tabs/orders/WorkPartsCrudController.cpp", "existingSnapshots");
    cs::tests::requireContains("src/core/services/orders/PricingRecalculationService.cpp", "COALESCE(manual_markup_override,0)=0");
    cs::tests::requireNotContains("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp", "PriceInputModePurchaseImport");
    cs::tests::requireContains("src/core/sync/SyncPayloadUtils.cpp", "PART_PRICING_SYNC_IMPORT");
    cs::tests::requireContains("src/ui/common/dialogs/ReceivedScansDialog.cpp", "PART_PRICING_SCAN_IMPORT");
}

void RegressionMarkupRecalculationTests::importedPurchasePriceGetsPolicyMarkup()
{
    PartPricingInput input;
    input.partName = QStringLiteral("Фільтр масляний");
    input.store = QStringLiteral("Еліт");
    input.qty = 2.0;
    input.purchasePrice = 1000.0;
    input.sourceType = QStringLiteral("import");

    const PartPricingResult r = PartPricingEngine::calculate(input, policyWith(QStringLiteral("Еліт"), 20.0));
    QCOMPARE(r.purchasePrice, 1000.0);
    QCOMPARE(r.purchaseAmount, 2000.0);
    QCOMPARE(r.clientPrice, 1200.0);
    QCOMPARE(r.clientAmount, 2400.0);
    QCOMPARE(r.markupPercent, 20.0);
    QCOMPARE(r.markupSource, QStringLiteral("policy"));
    QVERIFY(!r.manualMarkupOverride);
}

void RegressionMarkupRecalculationTests::manualInlineClientPriceBackCalculatesPurchasePrice()
{
    PartPricingInput input;
    input.partName = QStringLiteral("Свічка запалювання");
    input.store = QStringLiteral("Автонова-Д");
    input.qty = 1.0;
    input.purchasePrice = 1200.0;
    input.sourceType = QStringLiteral("manual_inline");

    const PartPricingResult r = PartPricingEngine::calculate(input, policyWith(QStringLiteral("Автонова-Д"), 20.0));
    QCOMPARE(r.purchasePrice, 1200.0);
    QCOMPARE(r.clientPrice, 1440.0);
    QCOMPARE(r.markupPercent, 20.0);
    QVERIFY(!r.manualMarkupOverride);
}

void RegressionMarkupRecalculationTests::manualOverrideHasPriorityOverPolicy()
{
    PartPricingInput input;
    input.partName = QStringLiteral("Комплект ГРМ");
    input.store = QStringLiteral("Еліт");
    input.qty = 1.0;
    input.purchasePrice = 1000.0;
    input.manualMarkupOverride = true;
    input.manualMarkupPercent = 35.0;
    input.sourceType = QStringLiteral("manual_batch");

    const PartPricingResult r = PartPricingEngine::calculate(input, policyWith(QStringLiteral("Еліт"), 10.0));
    QCOMPARE(r.purchasePrice, 1000.0);
    QCOMPARE(r.clientPrice, 1350.0);
    QCOMPARE(r.markupPercent, 35.0);
    QCOMPARE(r.markupSource, QStringLiteral("manual"));
    QVERIFY(r.manualMarkupOverride);
}


void RegressionMarkupRecalculationTests::manualOverrideZeroMeansPurchaseEqualsClient()
{
    PartPricingInput input;
    input.partName = QStringLiteral("Фільтр паливний");
    input.store = QStringLiteral("Еліт");
    input.qty = 2.0;
    input.purchasePrice = 100.0;
    input.manualMarkupOverride = true;
    input.manualMarkupPercent = 0.0;
    input.sourceType = QStringLiteral("manual_inline");

    const PartPricingResult r = PartPricingEngine::calculate(input, policyWith(QStringLiteral("Еліт"), 24.0));
    QCOMPARE(r.purchasePrice, 100.0);
    QCOMPARE(r.purchaseAmount, 200.0);
    QCOMPARE(r.clientPrice, 100.0);
    QCOMPARE(r.clientAmount, 200.0);
    QCOMPARE(r.markupPercent, 0.0);
    QCOMPARE(r.markupSource, QStringLiteral("manual"));
    QVERIFY(r.manualMarkupOverride);
}

void RegressionMarkupRecalculationTests::globalPolicyRecalcMustPreserveManualOverrideContract()
{
    PartPricingInput manual;
    manual.partName = QStringLiteral("Амортизатор");
    manual.store = QStringLiteral("Еліт");
    manual.qty = 1.0;
    manual.purchasePrice = 1000.0;
    manual.manualMarkupOverride = true;
    manual.manualMarkupPercent = 35.0;
    manual.sourceType = QStringLiteral("settings_recalc");

    const PartPricingResult afterPolicy10 = PartPricingEngine::calculate(manual, policyWith(QStringLiteral("Еліт"), 10.0));
    const PartPricingResult afterPolicy50 = PartPricingEngine::calculate(manual, policyWith(QStringLiteral("Еліт"), 50.0));
    QCOMPARE(afterPolicy10.markupPercent, 35.0);
    QCOMPARE(afterPolicy50.markupPercent, 35.0);
    QCOMPARE(afterPolicy10.clientPrice, 1350.0);
    QCOMPARE(afterPolicy50.clientPrice, 1350.0);
    QVERIFY(afterPolicy10.manualMarkupOverride);
    QVERIFY(afterPolicy50.manualMarkupOverride);
}

void RegressionMarkupRecalculationTests::sameInputCannotBeMarkedUpTwiceAfterReopen()
{
    PartPricingInput first;
    first.partName = QStringLiteral("Підшипник");
    first.store = QStringLiteral("Еліт");
    first.qty = 1.0;
    first.purchasePrice = 1000.0;
    first.sourceType = QStringLiteral("import");
    const auto policy = policyWith(QStringLiteral("Еліт"), 20.0);
    const PartPricingResult saved = PartPricingEngine::calculate(first, policy);

    PartPricingInput reopen;
    reopen.partName = first.partName;
    reopen.store = first.store;
    reopen.qty = 1.0;
    reopen.purchasePrice = saved.purchasePrice;
    reopen.sourceType = QStringLiteral("manual_inline");
    const PartPricingResult restored = PartPricingEngine::calculate(reopen, policy);

    QCOMPARE(restored.purchasePrice, 1000.0);
    QCOMPARE(restored.clientPrice, 1200.0);
    QCOMPARE(restored.clientAmount, 1200.0);
}

void RegressionMarkupRecalculationTests::emptyStoreIgnoresGlobalPartsMarkupFallback()
{
    PartMarkupPolicyService::PolicySnapshot snapshot;
    snapshot.defaultPartsMarkupPercent = 20.0;
    snapshot.storeMarkups.insert(QStringLiteral("Еліт"), 20.0);
    QCOMPARE(PartMarkupPolicyService::effectivePartMarkupPercent(QStringLiteral("Фільтр салону"), QString(), snapshot), 0.0);
}

void RegressionMarkupRecalculationTests::motorOilWithEmptyStoreIgnoresGlobalFallbacks()
{
    PartMarkupPolicyService::PolicySnapshot snapshot;
    snapshot.defaultPartsMarkupPercent = 20.0;
    snapshot.defaultOilMarkupPercent = 30.0;
    PartMarkupPolicyService::OilMarkupRule emptyStoreRule;
    emptyStoreRule.oilGrade = QStringLiteral("5W40");
    emptyStoreRule.pct = 40.0;
    snapshot.oilRules.append(emptyStoreRule);
    QCOMPARE(PartMarkupPolicyService::effectivePartMarkupPercent(QStringLiteral("Масло моторне 5W40"), QString(), snapshot), 0.0);
}

void RegressionMarkupRecalculationTests::zeroOilRuleFallsBackOnlyToConfiguredStoreMarkup()
{
    PartMarkupPolicyService::PolicySnapshot snapshot;
    snapshot.storeMarkups.insert(QStringLiteral("База"), 18.0);
    PartMarkupPolicyService::OilMarkupRule rule;
    rule.store = QStringLiteral("База");
    rule.oilGrade = QStringLiteral("5W30");
    rule.pct = 0.0;
    snapshot.oilRules.append(rule);
    QCOMPARE(PartMarkupPolicyService::effectivePartMarkupPercent(QStringLiteral("Масло моторне 5W30"), QStringLiteral("База"), snapshot), 18.0);
}

void RegressionMarkupRecalculationTests::manualEditorPriceIsPersistedAsPurchaseBase()
{
    QVERIFY(connectedDialogSupport().contains(QStringLiteral("pricingInput.purchasePrice")));
    cs::tests::requireContains("src/ui/tabs/orders/WorkPartsPartPersistence.cpp", "payloadInput.purchasePrice");
    cs::tests::requireNotContains("src/ui/tabs/orders/WorkPartsPartPersistence.cpp", "payloadInput.priceInputMode");
}

void RegressionMarkupRecalculationTests::importedRowsUseOrderedDateAsOverdueAnchor()
{
    cs::tests::requireContains("src/core/services/orders/PartRowPayloadBuilderService.cpp", "import_delivery_date_from_ordered_at");
    const auto support = connectedDialogSupport();
    QVERIFY(support.contains(QStringLiteral("partEditorOverdueSeverity")));
    QVERIFY(support.contains(QStringLiteral("QColor(255, 160, 160)")));
    QVERIFY(support.contains(QStringLiteral("QColor(255, 235, 120)")));
}

void RegressionMarkupRecalculationTests::storeAliasesUseOneCanonicalPolicyKey()
{
    PartMarkupPolicyService::PolicySnapshot snapshot;
    snapshot.storeMarkups.insert(QStringLiteral("Автонова"), 25.0);
    QCOMPARE(PartMarkupPolicyService::effectivePartMarkupPercent(QStringLiteral("Опора кульова"), QStringLiteral("Автонова-Д"), snapshot), 25.0);
}

void RegressionMarkupRecalculationTests::reportsFallbackDoesNotTreatOilRuleAsKnownPartStore()
{
    PartMarkupPolicyService::PolicySnapshot snapshot;
    snapshot.storeMarkups.insert(QStringLiteral("База"), 15.0);
    PartMarkupPolicyService::OilMarkupRule rule;
    rule.store = QStringLiteral("База");
    rule.oilGrade = QStringLiteral("5W40");
    rule.pct = 0.0;
    snapshot.oilRules.append(rule);
    QCOMPARE(PartMarkupPolicyService::effectivePartMarkupPercent(QStringLiteral("Масло моторне 5W40"), QStringLiteral("База"), snapshot), 15.0);
}

void RegressionMarkupRecalculationTests::legacyRepairAndDbContextContractsRemainWired()
{
    cs::tests::requireContains("src/core/services/orders/PartMarkupPolicyService.cpp", "return defaultValue;");
    cs::tests::requireContains("src/core/services/settings/FinanceSettingsService.cpp", "return def;");
    cs::tests::requireContains("src/core/services/orders/PricingRecalculationService.cpp", "repairLegacyPricingMetadataAndFinancialSumsDetailed");
    cs::tests::requireContains("src/core/services/orders/PricingRecalculationService.cpp", "PART_PRICING op=repair order=0 partId=0 trace=legacy_metadata metadata=normalized financialSums=refreshed");
    cs::tests::requireContains("src/core/services/orders/PricingRecalculationService.cpp", "OrderFinancialService::refreshAllOrderFinancialSums(txDb, &sumsError)");
    cs::tests::requireContains("src/infrastructure/db/sqlite/DbBootstrapEnsureSchema.cpp", "UPDATE work_order_parts SET purchase_price = ROUND(price / (1.0 + markup_percent / 100.0), 2) WHERE purchase_price <= 0 AND price > 0 AND markup_percent > 0 AND COALESCE(manual_markup_override,0) = 0");
    cs::tests::requireContains("src/infrastructure/db/sqlite/DbCanonicalRewritePrimaryImportSupport.cpp", "pricing_calc_version");
    QVERIFY(reportsInvalidationConnected(
        cs::tests::readSourceFile("src/core/services/MutationPostCommitService.cpp"),
        cs::tests::readSourceFile("src/core/cache/SnapshotCacheInvalidator.cpp")));
    cs::tests::requireContains("src/ui/tabs/settings/SettingsFinanceSection.cpp", "Не перераховувати зараз");
}

void RegressionMarkupRecalculationTests::reportsDoNotKeepParallelStoreLookupOrGlobalDefaultReads()
{
    cs::tests::requireNotContains("src/core/services/ReportsPdfSupportLocalSupport.cpp", "lookupStoreMarkupPercentForReports");
    cs::tests::requireNotContains("src/core/services/ReportsDataLoaderLocalSupport.cpp", "lookupStoreMarkupPercentForReports");
    cs::tests::requireNotContains("src/core/services/ReportsPdfSupportLocalSupport.cpp", "effectiveMarkupPercentForReports");
    cs::tests::requireNotContains("src/core/services/ReportsDataLoaderLocalSupport.cpp", "effectiveMarkupPercentForReportsFromMaps");
    cs::tests::requireContains("src/core/services/ReportsPdfSupportSnapshotFlow.cpp", "intentionally not loaded here");
    cs::tests::requireContains("src/core/services/ReportsPdfSupportSnapshotFlow.cpp", "PartPricingEngine::calculate");
    cs::tests::requireContains("src/core/services/ReportsDataLoaderSnapshotFlow.cpp", "PartPricingEngine::calculate");
}

void RegressionMarkupRecalculationTests::disconnectedSupportAndCacheInvalidationAreRejected()
{
    auto entry = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsDialogSupport.cpp");
    const auto path = QStringLiteral("ui/tabs/orders/internal/WorkPartsDialogRowState.ipp");
    QVERIFY(cs::tests::sourceIncludesDirective(entry, path));
    entry.replace(QStringLiteral("#include \"") + path + QLatin1Char('"'), QString());
    QVERIFY(!cs::tests::sourceIncludesDirective(entry, path));
    const auto post = cs::tests::readSourceFile("src/core/services/MutationPostCommitService.cpp");
    const auto cache = cs::tests::readSourceFile("src/core/cache/SnapshotCacheInvalidator.cpp");
    QVERIFY(reportsInvalidationConnected(post, cache));
    auto disconnected = post;
    disconnected.replace(QStringLiteral("SnapshotCacheInvalidator::invalidateOrdersAndParts("), QStringLiteral("unused("));
    QVERIFY(!reportsInvalidationConnected(disconnected, cache));
    for (const auto& call : {QStringLiteral("invalidateReportsAndStats(reason);"), QStringLiteral("ReportsSnapshotCache::invalidateReports();")}) {
        auto broken = cache;
        broken.replace(call, QString());
        QVERIFY(!reportsInvalidationConnected(post, broken));
    }
}

QTEST_MAIN(RegressionMarkupRecalculationTests)
#include "RegressionMarkupRecalculationTests.moc"
