#pragma once

#include <QMap>
#include <QSqlDatabase>
#include <QString>
#include <QVariant>

bool restoreCanonicalRewriteWorkspaceSettings(QSqlDatabase database,
                                             const QMap<QString, QVariant>& workspaceSettingsSnapshot,
                                             QString* errorText = nullptr);

bool finalizeCanonicalRewriteImportedWorkspace(QSqlDatabase database,
                                              const QString& fingerprint,
                                              const QString& sourcePath,
                                              QString* errorText = nullptr);
