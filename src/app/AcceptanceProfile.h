#pragma once
#include <QString>

namespace AcceptanceProfile {
// Called only for the explicit isolated launch, before single-instance acquisition.
QString create(QString* error);
bool configureServices(const QString& root, QString* error);
}
