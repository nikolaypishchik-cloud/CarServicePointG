#pragma once

#include <QDate>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

struct PartRowSnapshot
{
    int id = 0;
    QString dateRaw;
    QString deliveryDateRaw;
    QString installedAtRaw;
    QString store;
    QString article;
    QString brand;
    QString name;
    double qty = 0.0;
    double price = 0.0;
    double amount = 0.0;
    double purchasePrice = 0.0;
    double purchaseAmount = 0.0;
    int orderNumber = 0;
    QString orderUid;
    QString orderDisplay;
    QString plate;
    QString carModel;
    QString vin;
    QString orderStatus;
    QString orderOpenedAtRaw;
    QDate orderOpenedDate;
    QString orderClosedAtRaw;
    QDate orderClosedDate;
    bool paid = false;
    double paidAmount = 0.0;
    bool hasInvoiceAttachment = false;
    QDate orderedDate;
    QDate deliveryDate;
    QDate installedDate;
    bool installed = false;
    bool highlightEligibleOrder = true;
    bool overdue = false;
    int overdueRank = 9;
    int overdueDays = 0;
    int overdueSeverity = 0; // 0=normal, 1=warning (3-4 days), 2=critical (5+ days)
    int searchRank = 0;
};

namespace PartsQueryService {

struct PartArchiveDateBounds
{
    QDate minDate;
    QDate maxDate;

    bool hasData() const { return minDate.isValid() && maxDate.isValid(); }
};

QDate parseBusinessDate(const QString& raw);
QString displayDate(const QString& raw);
PartArchiveDateBounds loadDateBounds(QSqlDatabase db, QString* errorText = nullptr);
PartArchiveDateBounds loadDateBounds(QString* errorText = nullptr);
QVector<PartRowSnapshot> loadRows(QSqlDatabase db, const QString& activeFilter, const QDate& from = QDate(), const QDate& to = QDate(), QString* errorText = nullptr);
QVector<PartRowSnapshot> loadRows(const QString& activeFilter, const QDate& from = QDate(), const QDate& to = QDate(), QString* errorText = nullptr);
void refreshDerivedFields(PartRowSnapshot* row, const QDate& today = QDate::currentDate());
PartRowSnapshot loadRowById(QSqlDatabase db, int partId, QString* errorText = nullptr);
PartRowSnapshot loadRowById(int partId, QString* errorText = nullptr);
void sortRowsForTable(QVector<PartRowSnapshot>* rows, const QDate& today = QDate());
bool updatePartDeliveryDate(QSqlDatabase db, int partId, const QString& deliveryDateIso, QString* errorText = nullptr);
// Worker-thread entry point owns connection selection and master filtering.
QVector<PartRowSnapshot> loadScopedRows(const QString& filter,const QDate& from,const QDate& to,
    bool masterOnly,const QString& account,QString* error);
void invalidateCache();

} // namespace PartsQueryService
