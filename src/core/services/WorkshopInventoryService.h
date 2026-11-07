#pragma once

#include <QDate>
#include <QString>
#include <QVector>

class QSqlDatabase;

struct WorkshopInventoryPurchaseInput
{
    QDate purchasedOn;
    QString materialName;
    QString unit;
    double quantity = 0.0;
    double unitCost = 0.0;
    QString supplier;
    QString note;
    double paidAmount = -1.0; // Existing callers retain the previously settled-purchase behavior.
    int deferredDays = 0;
    double clientPrice = -1.0; // Unset preserves pricing for legacy purchases.
    QString stockKind = QStringLiteral("material");
    QString category, article, batch, shelf, document;
    double minimumQuantity = 0.0;
    QDate orderedOn, dueOn, expiresOn;
    bool publishCommitted = true;
};

struct WorkshopInventoryMaterial
{
    QString key;
    QString name;
    QString unit;
};

struct WorkshopInventoryPurchaseRow
{
    qint64 id = 0;
    QDate purchasedOn;
    QString materialKey;
    QString materialName;
    QString unit;
    double quantity = 0.0;
    double unitCost = 0.0;
    QString supplier;
    QString note;
    double paidAmount = 0.0;
    double clientPrice = -1.0;
    QString category, article, batch, shelf, document;
    double minimumQuantity = 0.0;
};

struct WorkshopInventoryConsumptionRow
{
    qint64 partId = 0;
    QDate consumedOn;
    int orderNumber = 0;
    QString car;
    QString client;
    QString materialKey;
    QString materialName;
    QString unit;
    double quantity = 0.0;
    double unitCost = 0.0;
    double totalCost = 0.0;
};

struct WorkshopInventoryPeriodRow
{
    QDate date;
    QString operation;
    int orderNumber = 0;
    QString materialName;
    QString unit;
    double receivedQuantity = 0.0;
    double consumedQuantity = 0.0;
    double balanceQuantity = 0.0;
    double unitCost = 0.0;
    double totalCost = 0.0;
    qint64 purchaseId = 0;
    double stockValue = 0.0;
};

struct WorkshopInventoryBalanceRow
{
    QString materialKey;
    QString materialName;
    QString unit;
    double receivedQuantity = 0.0;
    double consumedQuantity = 0.0;
    double balanceQuantity = 0.0;
    double averageCost = 0.0;
    double stockValue = 0.0;
    double minimumQuantity = 0.0;
    QString shelf;
    QDate lastMovement;
};

struct WorkshopInventorySnapshot
{
    QVector<WorkshopInventoryPurchaseRow> purchases;
    QVector<WorkshopInventoryConsumptionRow> consumptions;
    QVector<WorkshopInventoryPeriodRow> periodRows;
    QVector<WorkshopInventoryBalanceRow> balanceRows;
    QString errorText;
};

namespace WorkshopInventoryService {

QString normalizedMaterialKey(const QString& materialName);
QString canonicalMaterialName(const QString& materialName);

bool ensureSchema(QSqlDatabase database, QString* errorText = nullptr);
bool addPurchase(QSqlDatabase database, const WorkshopInventoryPurchaseInput& input, qint64* purchaseId = nullptr, QString* errorText = nullptr);
bool removePurchase(QSqlDatabase database, qint64 purchaseId, QString* errorText = nullptr);
bool setClientPrice(QSqlDatabase database, qint64 purchaseId, double price, QString* errorText = nullptr);
QVector<WorkshopInventoryMaterial> loadMaterials(QSqlDatabase database, QString* errorText = nullptr, bool initializeSchema = true);
WorkshopInventorySnapshot buildSnapshot(QSqlDatabase database, const QDate& from, const QDate& to, bool initializeSchema = true);

bool addPurchase(const WorkshopInventoryPurchaseInput& input, qint64* purchaseId = nullptr, QString* errorText = nullptr);
bool removePurchase(qint64 purchaseId, QString* errorText = nullptr);
QVector<WorkshopInventoryMaterial> loadMaterials(QString* errorText = nullptr);
WorkshopInventorySnapshot buildSnapshot(const QDate& from, const QDate& to);

} // namespace WorkshopInventoryService

namespace WorkshopInventoryService {
// UI entry points: connection and transaction ownership remain in the service.
bool setClientPrice(qint64 purchaseId, double price, QString* errorText = nullptr);
}
