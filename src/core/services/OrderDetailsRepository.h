#pragma once

#include "core/repository/PartRepository.h"
#include "core/repository/ServiceRepository.h"

#include <QList>
#include <QSqlDatabase>
#include <QString>

namespace OrderDetailsRepository {

QList<WorkOrderServiceRow> listServicesByInternalNumber(QSqlDatabase database,
                                                        int orderNumber,
                                                        QString* errorText = nullptr);
QList<WorkOrderPartRow> listPartsByInternalNumber(QSqlDatabase database,
                                                  int orderNumber,
                                                  QString* errorText = nullptr);
QList<WorkOrderServiceRow> listServicesByUid(QSqlDatabase database,
                                             const QString& orderUid,
                                             QString* errorText = nullptr);
QList<WorkOrderPartRow> listPartsByUid(QSqlDatabase database,
                                       const QString& orderUid,
                                       QString* errorText = nullptr);

} // namespace OrderDetailsRepository
