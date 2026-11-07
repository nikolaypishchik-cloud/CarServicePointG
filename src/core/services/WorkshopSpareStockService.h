#pragma once
#include <QDate>
#include <QString>
#include <QVector>
#include <QSqlDatabase>
#include <QJsonObject>

struct WorkshopSpareOrder {
    qint64 id = 0;
    QString name, article, brand, model, plate, vin, masterId, masterName, supplier, shelf, document;
    QString invoicePath, invoiceName;
    QJsonObject invoiceCatalogIdentity;
    QDate orderedOn, expectedOn, dueOn, deliveredOn;
    double quantity = 1, received = 0, issued = 0, unitCost = 0, clientPrice = -1;
    qint64 debtCents = 0;
    int unpaidDays = -1;
    int revision = 0;
};
struct WorkshopSpareReceipt {
    qint64 purchaseId = 0;
    QDate date;
    double quantity = 0;
};
namespace WorkshopSpareStockService {
struct ImportTarget { int number = 0, visibleNumber = 0; QString car, plate; };
bool importTargets(QVector<ImportTarget>* result, QString* error = nullptr);
bool issue(const WorkshopSpareOrder& expected, double quantity, int orderNumber,
           QString* error = nullptr, bool confirmDifferentVehicle = false);
bool cancelDraftIssue(qint64 spareId, int orderNumber, QString* error = nullptr);
bool ensureSchema(QSqlDatabase db, QString* error = nullptr);
bool addOrder(QSqlDatabase db, const WorkshopSpareOrder& input, qint64* id, QString* error = nullptr,
              const QDate& initialReceiptDate = {}, bool publishCommitted = true);
struct BatchLine { WorkshopSpareOrder part; int orderNumber = 0; };
bool addBatch(QSqlDatabase db, const QVector<BatchLine>& lines, const QDate& receiptDate, QString* error = nullptr);
QVector<WorkshopSpareOrder> orders(QSqlDatabase db, QString* error = nullptr, bool initializeSchema = true);
QVector<WorkshopSpareReceipt> receipts(QSqlDatabase db, qint64 id, QString* error = nullptr);
bool receive(QSqlDatabase db, const WorkshopSpareOrder& expected, double quantity, const QDate& date,
             double paidAmount, QString* error = nullptr, bool publishCommitted = true);
bool issue(QSqlDatabase db, const WorkshopSpareOrder& expected, double quantity, int orderNumber,
           QString* error = nullptr, bool confirmDifferentVehicle = false);
bool checkIssueTarget(QSqlDatabase db, const WorkshopSpareOrder& reserve, int orderNumber,
                      bool* differentVehicle, QString* description, QString* error = nullptr);
bool cancelDraftIssue(QSqlDatabase db, qint64 spareId, int orderNumber, QString* error = nullptr);
// Received returns preserve financial history and require refund/credit confirmation.
bool returnReserve(QSqlDatabase db, const WorkshopSpareOrder& expected, const QString& oem,
                   const QString& vehicleNode, QString* error = nullptr,
                   const QString& settlement = {}, qint64 expectedPaidCents = -1);
QVector<QPair<int, QString>> eligibleOrders(QSqlDatabase db, const WorkshopSpareOrder& reserve, QString* error = nullptr);
}

namespace WorkshopSpareStockService {
// UI entry points: connection and transaction ownership remain in the service.
QVector<WorkshopSpareOrder> orders(QString* error = nullptr);
QVector<WorkshopSpareReceipt> receipts(qint64 id, QString* error = nullptr);
bool addBatch(const QVector<BatchLine>& lines, const QDate& receiptDate, QString* error = nullptr);
bool receive(const WorkshopSpareOrder& expected, double quantity, const QDate& date, double paidAmount, QString* error = nullptr);
bool checkIssueTarget(const WorkshopSpareOrder& reserve, int orderNumber, bool* differentVehicle, QString* description, QString* error = nullptr);
bool returnReserve(const WorkshopSpareOrder& expected, const QString& oem, const QString& vehicleNode, QString* error = nullptr, const QString& settlement = {}, qint64 expectedPaidCents = -1);
QVector<QPair<int, QString>> eligibleOrders(const WorkshopSpareOrder& reserve, QString* error = nullptr);
}
