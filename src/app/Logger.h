#pragma once
#include <QString>

// Very small logging facade. Implementation uses Qt message handler installed in App::installLogHandler().
class Logger {
public:
    enum class Level { Debug, Info, Warn, Error };

    static void log(Level level, const QString& msg);
    static void debug(const QString& msg) { log(Level::Debug, msg); }
    static void info (const QString& msg) { log(Level::Info,  msg); }
    static void warn (const QString& msg) { log(Level::Warn,  msg); }
    static void error(const QString& msg) { log(Level::Error, msg); }
};
