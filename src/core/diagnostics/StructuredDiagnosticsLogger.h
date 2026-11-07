#pragma once

#include <QLoggingCategory>
#include <QDebug>
#include <QString>
#include <QStringList>
#include <QVariantMap>

Q_DECLARE_LOGGING_CATEGORY(csWorkPartsTraceLog)

namespace StructuredDiagnosticsLogger {

enum class Level {
    Info,
    Warning,
    Critical
};

struct Event {
    QString module;
    QString operation;
    int orderNumber = -1;
    QString orderUid;
    int partId = -1;
    QString rowUid;
    QString invoiceNumber;
    QString transactionId;
    QString correlationId;
    qint64 tableRevision = -1;
    QVariantMap details;
};

QString toCompactJson(const Event& event);
QString ensureCorrelationId(const Event& event);
QStringList breadcrumbsSnapshot();
QString captureStackTrace();
void recordBreadcrumb(Level level, const Event& event, const QString& compactJson);
void log(Level level, const Event& event);
void logException(const QString& module,
                  const QString& operation,
                  const QString& message,
                  const QString& technicalDetails,
                  const QVariantMap& context = QVariantMap());

} // namespace StructuredDiagnosticsLogger
