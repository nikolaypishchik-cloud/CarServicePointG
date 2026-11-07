#pragma once
#include "core/services/settings/GeneralSettingsService.h"
#include <QString>
#include <functional>

namespace ElitPdfOpenService {
struct Request {
    QString databasePath, account, identity, scope;
    GeneralSettingsService::ElitCredentials credentials;
};
struct Result { QString path, error; };
bool prepare(const QString& identity, Request* request, QString* error);
bool isCurrent(const Request& request);
// Execute in the worker thread. The SQL connection never escapes this call.
Result execute(const Request& request, const std::function<bool()>& continueWork);
}
