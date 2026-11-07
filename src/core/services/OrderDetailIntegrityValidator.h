#pragma once

#include <QSqlDatabase>
#include <QString>

namespace OrderDetailIntegrityValidator {

struct Report {
    bool ok = false;
    QString errorText;
    int orderCount = 0;
    int serviceCount = 0;
    int partCount = 0;
    int distinctServiceOrderUidCount = 0;
    int distinctPartOrderUidCount = 0;
    int orphanServiceCount = 0;
    int orphanPartCount = 0;
    int routerOrderUidMismatchCount = 0;
    int routerServiceMismatchCount = 0;
    int routerPartMismatchCount = 0;
};

bool ensureCanonicalDetailSchema(QSqlDatabase database, QString* errorText = nullptr);
bool repairCanonicalOrderUids(QSqlDatabase database, QString* errorText = nullptr);
Report analyze(QSqlDatabase database);
bool validateAfterImport(QSqlDatabase database, QString* errorText = nullptr);

} // namespace OrderDetailIntegrityValidator
