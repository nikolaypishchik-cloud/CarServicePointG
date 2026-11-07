#pragma once

#include "infrastructure/db/sqlite/MigrationOrderKeys.h"
#include <QMap>
#include <QSet>
#include <QSqlDatabase>
#include <QString>
#include <QVariant>

struct CanonicalRewriteSourceSchema {
    QSet<QString> masters;
    QSet<QString> orders;
    QSet<QString> services;
    QSet<QString> serviceMasters;
    QSet<QString> parts;
    QSet<QString> settings;
    QSet<QString> expenses;
    QSet<QString> planning;
    QSet<QString> catalog;
    QSet<QString> outsource;
};

bool loadCanonicalRewriteSourceSchema(QSqlDatabase database, const QString& schemaName,
                                      CanonicalRewriteSourceSchema* schema, QString* errorText);
bool snapshotWorkspaceSettingsForCanonicalRewrite(QSqlDatabase database, QMap<QString, QVariant>* snapshot, QString* errorText);
bool clearCanonicalRewriteWorkspaceTargets(QSqlDatabase database, QString* errorText);

// Called within the enclosing rewrite transaction, before completion is recorded.
bool recordCanonicalRewriteCorrespondence(QSqlDatabase database, const QString& sourceIdentity,
    const QString& sourceSchema, const CanonicalRewriteSourceSchema& schema, QString* errorText,
    const MigrationOrderKeys::Map& orderKeys = {});
