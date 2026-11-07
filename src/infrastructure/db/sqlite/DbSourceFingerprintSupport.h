#pragma once

#include <QString>

QString legacySourceFingerprint(const QString& path);

// Copy a checkpointed source and check the bytes that were actually written.
bool copyAuditedDatabase(const QString& source,const QString& destination,const QString& identity,QString* error);
