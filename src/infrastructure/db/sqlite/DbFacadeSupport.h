#pragma once

#include <QDate>
#include <QDateTime>
#include <QSet>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVariant>

QString dbRegistryConnectionName();
QString sqlQuoteText(QString value);
QString nonNullText(const QString& value);

QString mainConnectionName();
QString currentConnectionName();
bool shouldMirrorSettingToQSettings(const QString& key);
QVariant qSettingsFallbackValue(const QString& key);
void mirrorSettingToQSettings(const QString& key, const QVariant& value);
QString normalizedMasterDate(QString value);
QString sqlQuoteIdent(QString name);
QSet<QString> tableColumnsForDb(const QSqlDatabase& database, const QString& tableName, const QString& schemaName = QString());
bool tableExistsInDb(const QSqlDatabase& database, const QString& tableName, const QString& schemaName = QString());
QString tableCreateSqlForDb(const QSqlDatabase& database, const QString& tableName, const QString& schemaName = QString());
bool tableCreateSqlContains(const QSqlDatabase& database, const QString& tableName, const QString& needle, const QString& schemaName = QString());
bool ensureColumnForDb(QSqlDatabase database, const QString& tableName, const QString& columnName,
                       const QString& ddl, QString* errorText = nullptr);
QString firstAvailableExpr(const QSet<QString>& cols, const QStringList& names, const QString& fallbackSql);
QString nonNullTextSql(const QString& expr);
QString nonNullIntSql(const QString& expr, const QString& fallback = QStringLiteral("0"));
QString nonNullRealSql(const QString& expr, const QString& fallback = QStringLiteral("0"));
bool dropLegacyCompatibilityViews(QSqlDatabase database);
bool dropViewsReferencingTable(QSqlDatabase database, const QString& tableName);
bool dropFtsTriggersReferencingTable(QSqlDatabase database, const QString& tableName);
QString nonNullVariantText(const QVariant& value);
int orderYearFromStoredDateTime(QString value);

bool rebuildActiveServicesTableToOrderUid(QSqlDatabase database);
bool rebuildActivePartsTableToCanonical(QSqlDatabase database);
bool rebuildActiveOutsourceRepairsTableToOrderUid(QSqlDatabase database);
bool rebuildActiveOutsourceJobsTableToOrderUid(QSqlDatabase database);
bool ensureRouterCacheSchema(QSqlDatabase database);
bool refreshArchiveIntoRouterCache(QSqlDatabase database);
bool syncLocalRegistryMirrorFromRegistryFile(QSqlDatabase database, QString* errorText = nullptr);
bool rebuildRouterViews(QSqlDatabase database);
bool copyCachedOrderToActive(QSqlDatabase database, const QString& requestedCanonicalUid, int orderNumber, QString* errorText);
bool ensureLegacyArchiveSnapshot();
bool ensurePartRegistrySchemaFile();
bool rewriteLegacyToCanonicalWorkspaceIfNeeded(QSqlDatabase database, QString* errorText = nullptr);
bool importSingleArchivedOrderToActiveFromPath(const QString& archivePath, int orderNumber, QString* errorText = nullptr);
QString archiveShardsDirPath();
bool ensureLocalRegistryMirrorSchema(QSqlDatabase database);
bool upsertActiveOrdersIntoRegistry(QString* errorText = nullptr);
bool archiveSingleActiveOrderToShard(QSqlDatabase database, int orderNumber, QString* errorText);

bool rebuildRegistryDerivedState(QSqlDatabase database, QString* errorText = nullptr);
bool purgeHydratedArchiveCopiesFromActive(QSqlDatabase database, QString* errorText = nullptr);
bool forceRebuildRouterCacheFromActiveWorkspace(QSqlDatabase database, QString* errorText = nullptr);
