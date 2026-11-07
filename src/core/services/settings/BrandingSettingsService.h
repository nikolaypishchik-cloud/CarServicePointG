#pragma once

#include <QString>

namespace BrandingSettingsService {

struct BrandingSnapshot {
    QString name;
    QString phone;
    QString logoPath;
    double logoPosX = 0.04;
    double logoPosY = 0.04;
    double logoScale = 1.0;
    double logoContrast = 1.0;
    double logoSaturation = 1.0;
    double logoOpacity = 1.0;
};

struct BrandingSavePayload {
    QString name;
    QString phone;
    QString logoPath;
    double logoPosX = 0.04;
    double logoPosY = 0.04;
    double logoScale = 1.0;
    double logoContrast = 1.0;
    double logoSaturation = 1.0;
    double logoOpacity = 1.0;
};

bool save(const BrandingSavePayload& payload, bool* changed = nullptr);
bool saveLogoPath(const QString& logoPath, bool* changed = nullptr);
bool saveLogoPosition(double x, double y, bool* changed = nullptr);
bool saveLogoVisuals(double scale, double contrast, double saturation, double opacity, bool* changed = nullptr);
bool saveNamePhone(const QString& name, const QString& phone, bool* changed = nullptr);

BrandingSnapshot loadSnapshot();

} // namespace BrandingSettingsService
