#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QVariant>
#include <QList>
#include <QMap>
#include <QDate>
#include "core/model/MasterRow.h"

struct CatalogImportReport {
    int processedCount = 0;
    int eligibleCount = 0;
    int uniqueSourceCount = 0;
    int addedCount = 0;
    int updatedCount = 0;
    int mergedCount = 0;
    int skippedCount = 0;
    int skippedOilCount = 0;
    int conflictCount = 0;
    int errorCount = 0;
    int legacyImportedCount = 0;
    int archiveImportedCount = 0;
    int activeImportedCount = 0;
    int sourceLayerCount = 0;
    QString importBatchId;
    QString reportsDir;
    QString summaryText;
};


class Database
{
public:
    static bool prepareRuntime(QString* errorText = nullptr);
    static bool open(QString* errorText = nullptr);
    static bool openPrepared(QString* errorText = nullptr);
    static void close();
    static QSqlDatabase db();
    static QSqlDatabase db(QString* errorText);
    static bool ensureConnectionAlive(QString* errorText = nullptr);
    static bool ensureCatalogImportSchema(QString* errorText = nullptr);
    static bool importCatalogFromOrders(CatalogImportReport* report, QString* errorText = nullptr);

    static bool ensureSchema();
    static bool runStartupSchemaCheck(QString* errorText = nullptr);
    static bool runStartupMigrationCheck(QString* errorText = nullptr);
    static bool runStartupArchiveMaintenance(QString* errorText = nullptr);
    static bool runStartupRegistryRefresh(QString* errorText = nullptr);
    static bool archiveSingleActiveOrderToArchive(int orderNumber, QString* errorText = nullptr);
    static bool rotateActiveClosedOrders(QString* errorText = nullptr);
    static bool cleanupHydratedArchiveCopies(QString* errorText = nullptr);

    // Masters
    static QList<MasterRow> masters(bool onlyActive = true);
    static QList<MasterRow> mastersForPeriod(const QString& fromDateIso, const QString& toDateIso);
    static bool upsertMaster(const MasterRow& m);

    static bool setMasterActive(const QString& id, bool active, const QString& effectiveDateIso = QString());
    static bool deleteMasterIfUnusedOrArchive(const QString& id, const QString& effectiveDateIso = QString());

    // Payroll percents per master (stored in settings JSON)
    static QMap<QString, double> payrollPercents();
    static double effectivePayrollPercent(const QString& masterId, const QString& masterName = QString());
    static bool setPayrollPercent(const QString& masterId, double percent);
    static bool removePayrollPercent(const QString& masterId);

    // Summary for unpaid parts (qty, sum client amount)
    static bool unpaidPurchasesSummary(int& countOut, double& sumOut);
    static bool setPartPaid(int partId, bool paid);
    static bool setPartInstalledAt(int partId, const QString& installedAtIso);

    // Simple key-value settings (stored in DB)
    static QVariant getSetting(const QString& key, const QVariant& def = QVariant());
    static bool setSetting(const QString& key, const QVariant& value);

    // DB router / registry helpers
    static int nextOrderNumber();
    static int nextOrderInternalNumber();
    static int nextOrderNumberForDate(const QDate& openedDate);
    static QString resolveOrderUid(int orderNumber);
    static int resolveOrderInternalNumberByUid(const QString& orderUid);
    static bool ensureOrderReadyForInteractiveOpen(int orderNumber, QString* errorText = nullptr);
    static bool ensureOrderRuntimeStateForInteractiveOpen(int orderNumber, QString* errorText = nullptr);
    static bool ensureOrderAvailableInActive(int orderNumber, QString* errorText = nullptr);
    static bool ensureOrderAvailableInActiveByUid(const QString& orderUid, QString* errorText = nullptr);
    static bool finalizeNewOrderWorkspaceState(int orderNumber, QString* errorText = nullptr);
    // Caller must own an active TransactionContext on this connection.
    static bool finalizeOrderWorkspaceInTransaction(QSqlDatabase database, int orderNumber, QString* errorText = nullptr);
    // Rebuildable external projections and rotation run only after local COMMIT.
    static void publishFinalizedOrderWorkspace(int orderNumber);
    static bool finalizeNewOrderWorkspaceStateByUid(const QString& orderUid, QString* errorText = nullptr);
    static bool replayPendingOrderRegistryRefresh(QString* errorText = nullptr);
    static bool refreshOrderRegistry(int orderNumber, QString* errorText = nullptr);
    static bool refreshOrderRegistryByUid(const QString& orderUid, QString* errorText = nullptr);
    static bool hardDeleteOrder(int orderNumber, QString* errorText = nullptr);
    static bool removeOrderFromRegistry(int orderNumber, QString* errorText = nullptr);
    static bool removeOrderFromRegistryByUid(const QString& orderUid, QString* errorText = nullptr);

private:
    static bool ensureColumn(const QString& table, const QString& colName, const QString& ddl);
    static bool tableExists(const QString& table);
};
