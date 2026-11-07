#pragma once

#include <QString>

namespace RuntimeDbPreflightValidator {

enum class DbRole {
    Workspace,
    Registry,
    Archive
};

bool validateFile(const QString& dbPath, DbRole role, QString* errorText = nullptr);
QString roleName(DbRole role);

} // namespace RuntimeDbPreflightValidator
