#include "core/diagnostics/StructuredDiagnosticsLogger.h"

#include <QDateTime>
#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <cstdlib>
#include <QtGlobal>

#if defined(Q_OS_UNIX) && __has_include(<execinfo.h>)
#include <execinfo.h>
#endif

Q_LOGGING_CATEGORY(csWorkPartsTraceLog, "carservice.workparts.trace")

namespace StructuredDiagnosticsLogger {
namespace {

constexpr int kMaxBreadcrumbs = 100;
QMutex& breadcrumbMutex()
{
    static QMutex mutex;
    return mutex;
}

QStringList& breadcrumbRing()
{
    static QStringList ring;
    return ring;
}

QString levelName(Level level)
{
    switch (level) {
    case Level::Info: return QStringLiteral("info");
    case Level::Warning: return QStringLiteral("warning");
    case Level::Critical: return QStringLiteral("critical");
    }
    return QStringLiteral("unknown");
}

QJsonValue toJsonValue(const QVariant& value)
{
    if (!value.isValid()) return QJsonValue();
    switch (value.metaType().id()) {
    case QMetaType::Bool:
        return QJsonValue(value.toBool());
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return QJsonValue::fromVariant(value);
    case QMetaType::Double:
    case QMetaType::Float:
        return QJsonValue(value.toDouble());
    case QMetaType::QStringList:
        return QJsonArray::fromStringList(value.toStringList());
    default:
        return QJsonValue(value.toString());
    }
}

QJsonObject detailsObject(const QVariantMap& details)
{
    QJsonObject out;
    for (auto it = details.cbegin(); it != details.cend(); ++it) {
        out.insert(it.key(), toJsonValue(it.value()));
    }
    return out;
}

QString threadIdHex()
{
    return QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16);
}

bool deepDiagnosticsEnabled()
{
    const QByteArray env = qgetenv("CS_DEEP_DEBUG").trimmed();
    return env == "1" || env.compare("true", Qt::CaseInsensitive) == 0;
}

bool diagnosticRateLimitAllows(const QString& key, qint64 windowMs = 10000)
{
    if (deepDiagnosticsEnabled()) return true;

    static QMutex mutex;
    static QHash<QString, qint64> lastSeenMs;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString normalizedKey = key.left(240);

    QMutexLocker locker(&mutex);
    const auto it = lastSeenMs.constFind(normalizedKey);
    if (it != lastSeenMs.constEnd() && now - *it < windowMs) {
        return false;
    }
    if (lastSeenMs.size() > 512) {
        lastSeenMs.clear();
    }
    lastSeenMs.insert(normalizedKey, now);
    return true;
}

bool isProductionImportantDiagnostic(Level level, const Event& event)
{
    if (deepDiagnosticsEnabled()) return true;
    if (level == Level::Critical) return true;

    const QString op = event.operation.trimmed().toLower();
    if (op.contains(QStringLiteral("failed"))
        || op.contains(QStringLiteral("error"))
        || op.contains(QStringLiteral("timeout"))
        || op.contains(QStringLiteral("crash"))
        || op.contains(QStringLiteral("exception"))) {
        return true;
    }

    // Hot-path editor/model events are preserved as breadcrumbs for crash
    // reports but are intentionally not written to production logs.
    return false;
}

} // namespace

QString ensureCorrelationId(const Event& event)
{
    const QString explicitId = event.correlationId.trimmed();
    if (!explicitId.isEmpty()) return explicitId;

    const QString module = event.module.trimmed().isEmpty() ? QStringLiteral("module") : event.module.trimmed();
    const QString operation = event.operation.trimmed().isEmpty() ? QStringLiteral("operation") : event.operation.trimmed();
    const qint64 stamp = QDateTime::currentMSecsSinceEpoch();
    return QStringLiteral("%1:%2:%3:%4:%5")
        .arg(module, operation, threadIdHex())
        .arg(static_cast<qint64>(event.tableRevision))
        .arg(stamp);
}

QString toCompactJson(const Event& sourceEvent)
{
    Event event = sourceEvent;
    event.correlationId = ensureCorrelationId(event);

    QJsonObject obj;
    obj.insert(QStringLiteral("timestampUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    obj.insert(QStringLiteral("module"), event.module);
    obj.insert(QStringLiteral("operation"), event.operation);
    obj.insert(QStringLiteral("orderNumber"), event.orderNumber);
    obj.insert(QStringLiteral("orderUid"), event.orderUid);
    obj.insert(QStringLiteral("partId"), event.partId);
    obj.insert(QStringLiteral("rowUid"), event.rowUid);
    obj.insert(QStringLiteral("invoiceNumber"), event.invoiceNumber);
    obj.insert(QStringLiteral("transactionId"), event.transactionId);
    obj.insert(QStringLiteral("correlationId"), event.correlationId);
    obj.insert(QStringLiteral("threadId"), threadIdHex());
    obj.insert(QStringLiteral("tableRevision"), static_cast<qint64>(event.tableRevision));
    obj.insert(QStringLiteral("details"), detailsObject(event.details));
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QStringList breadcrumbsSnapshot()
{
    QMutexLocker locker(&breadcrumbMutex());
    return breadcrumbRing();
}

QString captureStackTrace()
{
#if defined(Q_OS_UNIX) && __has_include(<execinfo.h>)
    void* frames[32] = {};
    const int frameCount = backtrace(frames, 32);
    char** symbols = backtrace_symbols(frames, frameCount);
    QStringList out;
    if (symbols) {
        for (int i = 0; i < frameCount; ++i) {
            out << QString::fromLocal8Bit(symbols[i]);
        }
        free(symbols);
    }
    return out.join(QStringLiteral(" | "));
#else
    return QStringLiteral("stack_trace_unavailable_on_this_platform");
#endif
}

void recordBreadcrumb(Level level, const Event& event, const QString& compactJson)
{
    QJsonObject breadcrumb;
    breadcrumb.insert(QStringLiteral("timestampUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    breadcrumb.insert(QStringLiteral("level"), levelName(level));
    breadcrumb.insert(QStringLiteral("module"), event.module);
    breadcrumb.insert(QStringLiteral("operation"), event.operation);
    breadcrumb.insert(QStringLiteral("correlationId"), ensureCorrelationId(event));
    breadcrumb.insert(QStringLiteral("threadId"), threadIdHex());
    breadcrumb.insert(QStringLiteral("event"), compactJson);

    QMutexLocker locker(&breadcrumbMutex());
    QStringList& ring = breadcrumbRing();
    ring.append(QString::fromUtf8(QJsonDocument(breadcrumb).toJson(QJsonDocument::Compact)));
    while (ring.size() > kMaxBreadcrumbs) {
        ring.removeFirst();
    }
}

void log(Level level, const Event& sourceEvent)
{
    Event event = sourceEvent;
    event.correlationId = ensureCorrelationId(event);
    const QString line = QStringLiteral("WORKPARTS_DIAGNOSTIC ") + toCompactJson(event);
    recordBreadcrumb(level, event, line);

    if (!isProductionImportantDiagnostic(level, event)) {
        return;
    }

    const QString rateKey = QStringLiteral("%1:%2:%3")
        .arg(levelName(level), event.module, event.operation);
    if (!diagnosticRateLimitAllows(rateKey)) {
        return;
    }

    switch (level) {
    case Level::Info:
        if (deepDiagnosticsEnabled()) {
            qCInfo(csWorkPartsTraceLog).noquote() << line;
        }
        break;
    case Level::Warning:
        qCWarning(csWorkPartsTraceLog).noquote() << line;
        break;
    case Level::Critical:
        qCCritical(csWorkPartsTraceLog).noquote() << line;
        break;
    }
}

void logException(const QString& module,
                  const QString& operation,
                  const QString& message,
                  const QString& technicalDetails,
                  const QVariantMap& context)
{
    Event event;
    event.module = module;
    event.operation = operation;
    event.correlationId = QStringLiteral("exception:%1:%2:%3:%4")
        .arg(module, operation, threadIdHex())
        .arg(QDateTime::currentMSecsSinceEpoch());
    event.details = context;
    event.details.insert(QStringLiteral("message"), message);
    event.details.insert(QStringLiteral("technicalDetails"), technicalDetails);
    event.details.insert(QStringLiteral("stackTrace"), captureStackTrace());
    event.details.insert(QStringLiteral("breadcrumbs"), breadcrumbsSnapshot());
    log(Level::Critical, event);
}

} // namespace StructuredDiagnosticsLogger
