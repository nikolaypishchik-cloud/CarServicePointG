#pragma once
#include <QHash>
#include <QString>
#include <QVector>
#include <QSqlDatabase>
#include <QDate>
struct PartRowSnapshot;
struct RegisteredInvoice;

struct PurchaseDebt {
    QString source;
    qint64 id = 0;
    QString orderUid, date, name, supplier, oem, invoice, car, note, unit;
    int orderNumber = 0;
    double quantity = 0;
    QDate dueDate;
    qint64 totalCents = 0, paidCents = 0;
    qint64 remainingCents() const { return qMax<qint64>(0, totalCents - paidCents); }
};
struct PurchasePaymentEntry {
    QString date, actor;
    qint64 amountCents = 0;
    QDate dueDate;
};
namespace PurchasePaymentService {
struct OrderPaymentPreparation {
    QVector<PurchaseDebt> expectedDebts;
    QVector<PurchaseDebt> displayedPurchases;
};
OrderPaymentPreparation prepareOrderPayment(int orderNumber, QString* error);
OrderPaymentPreparation prepareOrderPayment(QSqlDatabase db, int orderNumber, QString* error);
bool canPayInvoice(const RegisteredInvoice& invoice);
bool payOrderParts(int orderNumber, const QVector<PurchaseDebt>& expected, QString* error, qint64 amountCents);
bool payInvoice(const RegisteredInvoice& expected, QString* error, qint64 amountCents);
// UI entry points own selection of the application's database connection.
PurchaseDebt detail(const QString& source, qint64 id, const QString& orderUid, QString* error = nullptr);
bool canPay(const QString& source);
bool pay(const PurchaseDebt& expected, qint64 amountCents, QString* error = nullptr, int deferredDays = -1);
qint64 cents(double amount);
QString key(const QString& source, qint64 id, const QString& orderUid = {});
bool ensureSchema(QSqlDatabase db, QString* error = nullptr);
QHash<QString, qint64> balances(QSqlDatabase db, QString* error = nullptr);
void apply(PartRowSnapshot& row, const QHash<QString, qint64>& balances);
PurchaseDebt fromPart(const PartRowSnapshot& row);
// -1 means the ordering date is unknown; unpaid closed orders keep aging.
int unpaidDays(const PurchaseDebt& debt, const PartRowSnapshot* part, const QDate& today);
PurchaseDebt detail(QSqlDatabase db, const QString& source, qint64 id, const QString& orderUid, QString* error = nullptr);
QVector<PurchaseDebt> debts(QSqlDatabase db, QString* error = nullptr);
QVector<PurchasePaymentEntry> history(QSqlDatabase db, const PurchaseDebt& debt, QString* error = nullptr);
QVector<PurchasePaymentEntry> history(const PurchaseDebt& debt, QString* error = nullptr);
QVector<PurchasePaymentEntry> groupHistory(QSqlDatabase db, const QVector<PurchaseDebt>& debts, QString* error = nullptr);
QVector<PurchasePaymentEntry> groupHistory(const QVector<PurchaseDebt>& debts, QString* error = nullptr);
bool canPay(QSqlDatabase db, const QString& source);
QVector<PurchaseDebt> orderPartDebts(QSqlDatabase db, int orderNumber, QString* error = nullptr);
bool payOrderParts(QSqlDatabase db, int orderNumber, const QVector<PurchaseDebt>& expected, QString* error = nullptr, qint64 amountCents = -1);
bool payInvoice(QSqlDatabase db, const RegisteredInvoice& expected, QString* error = nullptr, qint64 amountCents = -1);
bool payInvoiceOrders(QSqlDatabase db, const RegisteredInvoice& expected, const QVector<int>& orderNumbers, QString* error = nullptr, qint64 amountCents = -1);
// deferredDays: -1 preserves the current date, 0 clears it, positive values count from today.
bool pay(QSqlDatabase db, const PurchaseDebt& expected, qint64 amountCents, QString* error = nullptr, int deferredDays = -1);
bool resetPart(QSqlDatabase db, int id, QString* error = nullptr);
bool resetPart(int id,QString* error=nullptr);
bool refreshPayment(PartRowSnapshot& row,QString* error=nullptr);
// Called inside the inventory purchase transaction, including a zero-payment choice.
bool initializeInventory(QSqlDatabase db, qint64 id, qint64 paidCents, QString* error = nullptr, int deferredDays = 0);
}
