#include "core/diagnostics/RuntimeLoggingControl.h"

#include <QByteArray>
#include <QLoggingCategory>
#include <QtGlobal>

namespace {
QByteArray runtimeLoggingRules()
{
    return QByteArrayLiteral(
        "*.debug=false\n"
        "qt.*.debug=false\n"
        "qt.qpa.*=false\n"
        "qt.core.plugin.*=false\n"
        "qt.highdpi.*=false\n"
        "qt.text.*=false\n"
        "qt.accessibility.*=false\n"
        "qt.widgets.painting=false\n"
        "qt.widgets.focus=false\n"
        "qt.gui.focus=false\n");
}
}

bool RuntimeLoggingControl::deepDebugEnabled()
{
    const QByteArray env = qgetenv("CS_DEEP_DEBUG").trimmed();
    return env == "1" || env.compare("true", Qt::CaseInsensitive) == 0;
}

void RuntimeLoggingControl::configureEnvironmentForRuntime()
{
    if (deepDebugEnabled()) return;
    const QByteArray rules = runtimeLoggingRules();
    qputenv("QT_LOGGING_RULES", rules);
    qunsetenv("QT_MESSAGE_PATTERN");
    qputenv("QT_FORCE_STDERR_LOGGING", QByteArrayLiteral("0"));
    qputenv("QT_DEBUG_PLUGINS", QByteArrayLiteral("0"));
}

void RuntimeLoggingControl::configureForRuntime()
{
    configureEnvironmentForRuntime();
    if (deepDebugEnabled()) return;
    QLoggingCategory::setFilterRules(QString::fromUtf8(runtimeLoggingRules()));
}
