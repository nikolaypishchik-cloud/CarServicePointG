#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QString>

class PartServiceAssignmentService
{
public:
    struct ServiceRow {
        int id = 0;
        QString title;
    };

    struct PartRow {
        int id = 0;
        QString name;
        int serviceId = 0;
    };

    static int autoAssign(const QString& orderUid,
                          const QList<ServiceRow>& services,
                          const QList<PartRow>& parts,
                          QString* errorText);

    static int autoAssign(QSqlDatabase db,
                          const QString& orderUid,
                          const QList<ServiceRow>& services,
                          const QList<PartRow>& parts,
                          QString* errorText);
};
