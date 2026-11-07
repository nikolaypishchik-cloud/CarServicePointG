#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QVariantMap>

namespace CrmTaskService {

// One user operation: task, client timeline, audit and sync intents commit together.
bool createTaskWithTimeline(const QString& clientUid, const QString& vehicleUid,
                            const QString& title, const QString& dueAt,
                            QString* errorText = nullptr, QSqlDatabase db = {},
                            const QString& requestId = {});

bool createTask(QSqlDatabase db,
                const QString& clientUid,
                const QString& vehicleUid,
                const QString& orderUid,
                const QString& title,
                const QString& dueAt,
                const QString& priority = QStringLiteral("normal"),
                const QString& assignedTo = QString(),
                QString* taskUid = nullptr,
                QString* errorText = nullptr);

// Direct helpers are reserved for migration/import code that already runs under
// a controlled transaction. UI/business writes must call createTask()/markCompleted().
bool createTaskDirect(QSqlDatabase db,
                      const QString& taskUid,
                      const QString& clientUid,
                      const QString& vehicleUid,
                      const QString& orderUid,
                      const QString& title,
                      const QString& dueAt,
                      const QString& priority = QStringLiteral("normal"),
                      const QString& assignedTo = QString(),
                      QString* errorText = nullptr);

bool markCompleted(QSqlDatabase db, const QString& taskUid, QString* errorText = nullptr);
bool markCompletedDirect(QSqlDatabase db, const QString& taskUid, QString* errorText = nullptr);

bool upsertTaskDirect(QSqlDatabase db,
                      const QString& taskUid,
                      const QVariantMap& fields,
                      QString* errorText = nullptr);

} // namespace CrmTaskService
