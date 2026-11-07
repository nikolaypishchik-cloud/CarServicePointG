#pragma once
#include <QString>
namespace DebtAnalysisPdfService {
// Shared presentation cleanup; never changes the accounting snapshot.
QString compactHtml(const QString& html);
bool save(const QString& path, const QString& analysisHtml, QString* error = nullptr);
}
