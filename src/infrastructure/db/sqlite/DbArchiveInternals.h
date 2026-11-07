#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QSet>

QString dbRegistryConnectionName();
QString sqlQuoteText(QString value);
QString nonNullText(const QString& value);
QString archiveShardFilePathForOrder(int orderNumber);
bool ensureArchiveShardSchemaByPath(const QString& dbPath);
bool ensureRegistrySchemaFile();
// Import one verified archive source without rebuilding unrelated projections.
bool importArchivedRegistryFromDbPath(const QString& dbPath, QString* errorText = nullptr, QSet<QString>* importedUids = nullptr);
// Publish inside caller-owned transactions without acknowledging source revisions.
bool publishCommittedOrderRegistryProjection(QSqlDatabase source, int orderNumber, QString* errorText = nullptr);
bool syncRegistryProjectionInOwnedTransaction(QSqlDatabase database, QString* errorText = nullptr);
QString makePartRegistryEntryKey(const QString& storageKind, const QString& storagePath, int orderNumber, int partRef,
                                 const QString& orderedAt, const QString& partNumber, const QString& name);
bool upsertPartRegistryFileRow(const QString& entryKey, int orderNumber, int partRef,
                               const QString& storageKind, const QString& storagePath, const QString& sourceDb,
                               const QString& orderedAt, const QString& deliveryDate,
                               const QString& partNumber, const QString& name, const QString& note,
                               const QString& plate, const QString& plateNorm,
                               const QString& vin, const QString& car, const QString& client, bool prepareSchema = true,
                               const QString& orderUid = QString(), const QString& partUid = QString());
bool removePartRegistryFileRows(int orderNumber, const QString& storageKind = QString(), const QString& storagePath = QString(), bool prepareSchema = true);
bool upsertRegistryMirrorRow(QSqlDatabase database, int orderNumber, const QString& orderUid, int visibleOrderNumber, int orderYear,
                             const QString& storageKind, const QString& storagePath, const QString& sourceDb,
                             const QString& importedAt, const QString& status,
                             const QString& openedAt, const QString& closedAt,
                             const QString& plate, const QString& plateNorm,
                             const QString& vin, const QString& car, const QString& client,
                             const QString& masterId, const QString& masterNameClosed,
                             double worksSum, double partsSum, bool prepareSchema = true);

void removeRegistryMirrorRow(QSqlDatabase database, int orderNumber);
bool syncPartRegistryFromLiveOrder(QSqlDatabase database, int orderNumber, const QString& storageKind,
                                  const QString& storagePath, const QString& sourceDb);
bool importSingleArchivedOrderToActiveFromPath(const QString& archivePath, int orderNumber, QString* errorText);
bool importSingleLegacyOrderToActive(int orderNumber, QString* errorText);
bool ensureWorkspaceOrderStateSchema(QSqlDatabase database);
bool purgeActiveOrderGraph(QSqlDatabase database, int orderNumber, QString* errorText = nullptr);
bool purgeVerifiedArchivedOrderGraph(QSqlDatabase database, int orderNumber, QString* errorText);
