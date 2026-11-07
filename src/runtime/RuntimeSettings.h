#pragma once

#include <QString>
#include <QVariant>

class RuntimeSettings
{
public:
    static QVariant get(const QString& key, const QVariant& def = QVariant());
    static void set(const QString& key, const QVariant& value);
};
