#pragma once

#include <QString>
#include <QStringList>

namespace PartImportTextSupport {

QString currentDisplayDate();
double parseLooseNumber(const QString& raw, bool* ok = nullptr);
QString titleCaseWords(QString s);
QString translateTechnicalRuToUa(QString s);
QString normalizeRecommendations(const QString& text);
QString normalizePartName(QString s);
QString normalizeImportedDateDisplay(QString raw);
QStringList knownStoreNames();

} // namespace PartImportTextSupport
