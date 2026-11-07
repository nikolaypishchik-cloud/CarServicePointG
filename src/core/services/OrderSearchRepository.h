#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QString>

struct OrderSearchHit {
    QString orderUid;
    int internalNumber = 0;
    int visibleNumber = 0;
    int orderYear = 0;
    QString openedAt;
    QString closedAt;
    QString status;
    QString plate;
    QString car;
    QString client;
    QString storageKind;
    QString storagePath;
};

namespace OrderSearchRepository {

QList<OrderSearchHit> findByVisibleNumber(QSqlDatabase database,
                                          int visibleNumber,
                                          int limit = 100,
                                          QString* errorText = nullptr);

} // namespace OrderSearchRepository
