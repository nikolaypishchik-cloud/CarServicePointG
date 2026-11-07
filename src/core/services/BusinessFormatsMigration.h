#pragma once
#include <QSqlDatabase>
#include <QString>

namespace BusinessFormatsMigration {
// Run only after a verified backup and compatibility checks of every consumer.
bool apply(QSqlDatabase db, QString* error = nullptr);
}
