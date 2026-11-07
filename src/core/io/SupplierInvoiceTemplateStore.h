#pragma once

#include <QJsonObject>
#include <QString>
#include <functional>

// Optional recognition hints, never invoice positions or payment records.
namespace SupplierInvoiceTemplateStore {
QString defaultPath();
QJsonObject read(const QString& path);
// Reads and writes under one cross-process lock. Busy or invalid stores fail
// without invoking the mutation; callers may retry an optional hint later.
bool update(const QString& path, const std::function<void(QJsonObject&)>& mutation,
            QString* errorText = nullptr);
}
