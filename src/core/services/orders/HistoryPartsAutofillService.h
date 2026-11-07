#pragma once

#include <QSqlDatabase>
#include <QString>
#include "core/repository/PartRepository.h"

namespace HistoryPartsAutofillService {
bool suggest(int orderNumber, const QString& title, QList<WorkOrderPartRow>* parts, QString* errorText = nullptr);


// Read-only lookup for a work row that has not been saved yet.
bool suggest(QSqlDatabase db, int orderNumber, const QString& title,
             QList<WorkOrderPartRow>* parts, QString* errorText = nullptr);
bool samePart(const WorkOrderPartRow& a, const WorkOrderPartRow& b);

// Called inside the same transaction that creates the service, after manual parts are saved.
bool apply(QSqlDatabase db, int orderNumber, int serviceId, QString* errorText = nullptr);

}
