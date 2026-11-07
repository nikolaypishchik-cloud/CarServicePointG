#pragma once

#include <QDate>
#include <QString>
#include <QVector>

namespace PartOverduePolicy {

enum Severity {
    SeverityNormal = 0,
    SeverityWarning = 1,
    SeverityCritical = 2,
};

enum Rank {
    RankCriticalUnpaidNotInstalled = 0,
    RankCriticalInstalledNotPaid = 1,
    RankWarningUnpaidNotInstalled = 2,
    RankWarningInstalledNotPaid = 3,
    RankActiveUnpaidNotOverdue = 4,
    RankActivePaidNotInstalled = 5,
    RankFinalizedOrClosed = 9,
};

struct Input
{
    QDate deliveryDate;
    QDate orderedDate;
    QDate installedDate;
    QDate orderClosedDate;
    QString orderStatus;
    QString orderClosedAtRaw;
    QString orderUid;
    int orderNumber = 0;
    int rowId = 0;
    bool paid = false;
};

struct Result
{
    int rank = RankFinalizedOrClosed;
    int severity = SeverityNormal;
    int overdueDays = 0;
    bool overdue = false;
    bool activeOrder = false;
    bool eligibleForOverdue = false;
};

struct OrderAggregate
{
    int sortRank = RankFinalizedOrClosed;
    int severity = SeverityNormal;
    bool overdue = false;
};

QDate businessToday(const QDate& today = QDate());
QString normalizedOrderStatus(QString status);
bool isClosedOrderStatus(const QString& normalizedStatus);
bool isActiveOrder(const QString& orderStatus, const QString& orderClosedAtRaw, const QDate& orderClosedDate = QDate());
bool isProblemRank(int rank);
bool isCriticalRank(int rank);
bool isWarningRank(int rank);
int severityForRank(int rank);
QString highlightColorNameForRank(int rank);
Result evaluate(const Input& input, const QDate& today = QDate());
void mergeIntoOrderAggregate(OrderAggregate* aggregate, const Result& policyResult);
OrderAggregate aggregateOrderResults(const QVector<Result>& results);

} // namespace PartOverduePolicy
