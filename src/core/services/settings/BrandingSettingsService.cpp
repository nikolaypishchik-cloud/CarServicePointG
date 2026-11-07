#include "core/services/settings/BrandingSettingsService.h"

#include "infrastructure/db/sqlite/Db.h"

#include <QtMath>

namespace BrandingSettingsService {

namespace {
constexpr double kEps = 0.0001;
}

bool save(const BrandingSavePayload& payload, bool* changed)
{
    const QString newName = payload.name.trimmed();
    const QString newPhone = payload.phone.trimmed();
    const QString newLogo = payload.logoPath.trimmed();

    const QString oldName = Database::getSetting(QStringLiteral("settings.branding.name"), QString()).toString().trimmed();
    const QString oldPhone = Database::getSetting(QStringLiteral("settings.branding.phone"), QString()).toString().trimmed();
    const QString oldLogo = Database::getSetting(QStringLiteral("settings.branding.logo"), QString()).toString().trimmed();
    const double oldPosX = Database::getSetting(QStringLiteral("settings.branding.logo_pos_x"), 0.04).toDouble();
    const double oldPosY = Database::getSetting(QStringLiteral("settings.branding.logo_pos_y"), 0.04).toDouble();
    const double oldScale = Database::getSetting(QStringLiteral("settings.branding.logo_scale"), 1.0).toDouble();
    const double oldContrast = Database::getSetting(QStringLiteral("settings.branding.logo_contrast"), 1.0).toDouble();
    const double oldSaturation = Database::getSetting(QStringLiteral("settings.branding.logo_saturation"), 1.0).toDouble();
    const double oldOpacity = Database::getSetting(QStringLiteral("settings.branding.logo_opacity"), 1.0).toDouble();

    const bool hasChanged = oldName != newName
        || oldPhone != newPhone
        || oldLogo != newLogo
        || qAbs(oldPosX - payload.logoPosX) > kEps
        || qAbs(oldPosY - payload.logoPosY) > kEps
        || qAbs(oldScale - payload.logoScale) > kEps
        || qAbs(oldContrast - payload.logoContrast) > kEps
        || qAbs(oldSaturation - payload.logoSaturation) > kEps
        || qAbs(oldOpacity - payload.logoOpacity) > kEps;

    Database::setSetting(QStringLiteral("settings.branding.name"), newName);
    Database::setSetting(QStringLiteral("settings.branding.phone"), newPhone);
    Database::setSetting(QStringLiteral("settings.branding.logo"), newLogo);
    Database::setSetting(QStringLiteral("settings.branding.logo_pos_x"), payload.logoPosX);
    Database::setSetting(QStringLiteral("settings.branding.logo_pos_y"), payload.logoPosY);
    Database::setSetting(QStringLiteral("settings.branding.logo_scale"), payload.logoScale);
    Database::setSetting(QStringLiteral("settings.branding.logo_contrast"), payload.logoContrast);
    Database::setSetting(QStringLiteral("settings.branding.logo_saturation"), payload.logoSaturation);
    Database::setSetting(QStringLiteral("settings.branding.logo_opacity"), payload.logoOpacity);

    if (changed) {
        *changed = hasChanged;
    }
    return true;
}

BrandingSnapshot loadSnapshot()
{
    BrandingSnapshot snapshot;
    snapshot.name = Database::getSetting(QStringLiteral("settings.branding.name"), QString()).toString().trimmed();
    snapshot.phone = Database::getSetting(QStringLiteral("settings.branding.phone"), QString()).toString().trimmed();
    snapshot.logoPath = Database::getSetting(QStringLiteral("settings.branding.logo"), QString()).toString().trimmed();
    snapshot.logoPosX = Database::getSetting(QStringLiteral("settings.branding.logo_pos_x"), 0.04).toDouble();
    snapshot.logoPosY = Database::getSetting(QStringLiteral("settings.branding.logo_pos_y"), 0.04).toDouble();
    snapshot.logoScale = Database::getSetting(QStringLiteral("settings.branding.logo_scale"), 1.0).toDouble();
    snapshot.logoContrast = Database::getSetting(QStringLiteral("settings.branding.logo_contrast"), 1.0).toDouble();
    snapshot.logoSaturation = Database::getSetting(QStringLiteral("settings.branding.logo_saturation"), 1.0).toDouble();
    snapshot.logoOpacity = Database::getSetting(QStringLiteral("settings.branding.logo_opacity"), 1.0).toDouble();
    return snapshot;
}

bool saveLogoPath(const QString& logoPath, bool* changed)
{
    const QString normalized = logoPath.trimmed();
    const bool hasChanged = Database::getSetting(QStringLiteral("settings.branding.logo"), QString()).toString().trimmed() != normalized;
    if (changed) *changed = hasChanged;
    Database::setSetting(QStringLiteral("settings.branding.logo"), normalized);
    return true;
}

bool saveLogoPosition(double x, double y, bool* changed)
{
    const double oldX = Database::getSetting(QStringLiteral("settings.branding.logo_pos_x"), 0.04).toDouble();
    const double oldY = Database::getSetting(QStringLiteral("settings.branding.logo_pos_y"), 0.04).toDouble();
    const bool hasChanged = qAbs(oldX - x) > kEps || qAbs(oldY - y) > kEps;
    if (changed) *changed = hasChanged;
    Database::setSetting(QStringLiteral("settings.branding.logo_pos_x"), x);
    Database::setSetting(QStringLiteral("settings.branding.logo_pos_y"), y);
    return true;
}

bool saveLogoVisuals(double scale, double contrast, double saturation, double opacity, bool* changed)
{
    const double oldScale = Database::getSetting(QStringLiteral("settings.branding.logo_scale"), 1.0).toDouble();
    const double oldContrast = Database::getSetting(QStringLiteral("settings.branding.logo_contrast"), 1.0).toDouble();
    const double oldSaturation = Database::getSetting(QStringLiteral("settings.branding.logo_saturation"), 1.0).toDouble();
    const double oldOpacity = Database::getSetting(QStringLiteral("settings.branding.logo_opacity"), 1.0).toDouble();
    const bool hasChanged = qAbs(oldScale - scale) > kEps
        || qAbs(oldContrast - contrast) > kEps
        || qAbs(oldSaturation - saturation) > kEps
        || qAbs(oldOpacity - opacity) > kEps;
    if (changed) *changed = hasChanged;
    Database::setSetting(QStringLiteral("settings.branding.logo_scale"), scale);
    Database::setSetting(QStringLiteral("settings.branding.logo_contrast"), contrast);
    Database::setSetting(QStringLiteral("settings.branding.logo_saturation"), saturation);
    Database::setSetting(QStringLiteral("settings.branding.logo_opacity"), opacity);
    return true;
}

bool saveNamePhone(const QString& name, const QString& phone, bool* changed)
{
    const QString newName = name.trimmed();
    const QString newPhone = phone.trimmed();
    const bool hasChanged = Database::getSetting(QStringLiteral("settings.branding.name"), QString()).toString().trimmed() != newName
        || Database::getSetting(QStringLiteral("settings.branding.phone"), QString()).toString().trimmed() != newPhone;
    if (changed) *changed = hasChanged;
    Database::setSetting(QStringLiteral("settings.branding.name"), newName);
    Database::setSetting(QStringLiteral("settings.branding.phone"), newPhone);
    return true;
}

} // namespace BrandingSettingsService
