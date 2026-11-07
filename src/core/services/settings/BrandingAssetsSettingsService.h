#pragma once

#include <QString>
#include <QStringList>

namespace BrandingAssetsSettingsService {

QStringList sanitizeLogoList(QStringList logos);
QStringList loadLogoList();
QStringList loadLogoListWithActive(const QString& activeLogoPath);
bool saveLogoList(const QStringList& logos, bool* changed = nullptr);
QString copyLogoToDataDir(const QString& sourcePath);
bool removeManagedLogoFile(const QString& path);

} // namespace BrandingAssetsSettingsService
