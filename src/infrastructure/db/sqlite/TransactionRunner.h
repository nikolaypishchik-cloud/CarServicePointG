#pragma once

#include <QSqlDatabase>
#include <QString>

#include <functional>

namespace TransactionRunner {

struct Result {
    bool ok = false;
    QString errorText;
};

using Step = std::function<bool(QString*)>;

Result run(QSqlDatabase db,
           const QString& beginErrorFallback,
           const QString& commitErrorFallback,
           const Step& step);

} // namespace TransactionRunner
