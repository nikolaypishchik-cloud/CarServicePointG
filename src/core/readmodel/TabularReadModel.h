#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

struct TabularReadModel
{
    QStringList headers;
    QVector<QVector<QVariant>> rows;
    QString errorText;

    bool hasError() const { return !errorText.trimmed().isEmpty(); }
    bool isEmpty() const { return rows.isEmpty(); }
};
