#include "app/Logger.h"
#include <QLoggingCategory>

static Q_LOGGING_CATEGORY(lgApp, "carservice.app")

void Logger::log(Level level, const QString& msg)
{
    switch (level) {
    case Level::Debug: qCDebug(lgApp)   << msg; break;
    case Level::Info:  qCInfo(lgApp)    << msg; break;
    case Level::Warn:  qCWarning(lgApp) << msg; break;
    case Level::Error: qCCritical(lgApp)<< msg; break;
    }
}
