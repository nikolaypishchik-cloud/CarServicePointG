#include "ui/tabs/schedule/PlanningPageSupport.h"

#include "core/util/PlateUtils.h"
#include "core/util/ManualInputNormalizer.h"
#include "core/util/VehicleNameUtils.h"
#include "core/util/SoundSupport.h"
#include "core/services/planning/PlanningQueryService.h"

#include <QApplication>
#include <QDebug>
#include <algorithm>
#include <QRegularExpression>
#include <QSet>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif

#if CS_HAS_MULTIMEDIA
#include <QSoundEffect>
#endif

namespace PlanningPageSupport {

#ifdef Q_OS_WIN
void forceReminderWindowOnTop(QWidget* widget)
{
    if (!widget) return;
    widget->raise();
    widget->activateWindow();

    HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    if (!hwnd) return;

    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
}
#else
void forceReminderWindowOnTop(QWidget* widget)
{
    if (!widget) return;
    widget->raise();
    widget->activateWindow();
}
#endif

bool reminderSoundsEnabled()
{
    return SoundSupport::soundsEnabled(true);
}

int reminderSoundsVolumePct()
{
    return SoundSupport::soundsVolumePct(95);
}

QByteArray loadReminderKnockBytes(QString* loadedFrom)
{
    return SoundSupport::loadAssetBytes(QStringLiteral("icq_triple_knock_same_speed_2x_louder.wav"),
                                        loadedFrom,
                                        QStringLiteral("Reminder sound"));
}

#if CS_HAS_MULTIMEDIA
static QSoundEffect* reminderKnockEffectInstance()
{
    static QSoundEffect* effect = []() {
        auto* e = new QSoundEffect();
        SoundSupport::setupEffect(e,
                                  QStringLiteral("icq_triple_knock_same_speed_2x_louder.wav"),
                                  1,
                                  0.95f,
                                  QStringLiteral("Reminder sound"));
        return e;
    }();
    return effect;
}
#endif

#ifdef Q_OS_WIN
static QString reminderKnockWavFilePath()
{
    static QString cachedPath;
    if (!cachedPath.isEmpty()) return cachedPath;

    QString loadedFrom;
    const QByteArray bytes = loadReminderKnockBytes(&loadedFrom);
    if (bytes.isEmpty()) {
        qWarning() << "Reminder sound path is empty";
        return QString();
    }

    cachedPath = SoundSupport::ensureTempWavFile(bytes,
                                                 QStringLiteral("icq_triple_knock_same_speed_2x_louder"),
                                                 loadedFrom,
                                                 QStringLiteral("Reminder sound loaded from:"));
    return cachedPath;
}

static bool playNativeWindowsReminderKnockSound()
{
    const QString path = reminderKnockWavFilePath();
    return SoundSupport::playNativeFile(path, true, false);
}
#endif

void playReminderKnockSound()
{
    if (!reminderSoundsEnabled()) {
        return;
    }

    const int volumePct = reminderSoundsVolumePct();
    bool played = false;
#ifdef Q_OS_WIN
    played = playNativeWindowsReminderKnockSound();
#endif
#if CS_HAS_MULTIMEDIA
    if (!played) {
        auto* effect = reminderKnockEffectInstance();
        effect->setMuted(false);
        effect->setVolume(qBound(0, volumePct, 100) / 100.0f);
        effect->stop();
        effect->play();
        played = true;
    }
#endif
    if (!played) {
        QApplication::beep();
        qWarning() << "All reminder sound backends failed; fallback beep used";
    }
}

QString uaWeekday(int dow)
{
    switch (dow) {
    case 1: return QString::fromUtf8("Понеділок");
    case 2: return QString::fromUtf8("Вівторок");
    case 3: return QString::fromUtf8("Середа");
    case 4: return QString::fromUtf8("Четвер");
    case 5: return QString::fromUtf8("Пʼятниця");
    case 6: return QString::fromUtf8("Субота");
    case 7: return QString::fromUtf8("Неділя");
    default: return QString();
    }
}

QString uaMonth(int m)
{
    static const char* months[] = {
        "Січень","Лютий","Березень","Квітень","Травень","Червень",
        "Липень","Серпень","Вересень","Жовтень","Листопад","Грудень"
    };
    if (m < 1 || m > 12) return QString();
    return QString::fromUtf8(months[m-1]);
}

bool isPlanningUpperAbbreviation(const QString& token)
{
    static const QSet<QString> abbreviations = {
        QString::fromUtf8("ТО"),
        QString::fromUtf8("СТО"),
        QString::fromUtf8("АБС"),
        QString::fromUtf8("ГРМ"),
        QString::fromUtf8("АКПП"),
        QString::fromUtf8("МКПП"),
        QString::fromUtf8("ГУР"),
        QString::fromUtf8("ЕГР"),
        QString::fromUtf8("ABS"),
        QString::fromUtf8("VIN"),
        QString::fromUtf8("OEM")
    };
    return abbreviations.contains(token.toUpper());
}

QString planningSentenceCaseToken(QString token, bool capitalizeFirstLetter)
{
    if (token.isEmpty()) return token;

    bool hasLetter = false;
    for (const QChar ch : token) {
        if (ch.isLetter()) {
            hasLetter = true;
            break;
        }
    }
    if (!hasLetter) return token;

    const QString upperToken = token.toUpper();
    if (isPlanningUpperAbbreviation(upperToken)) return upperToken;

    token = token.toLower();
    if (capitalizeFirstLetter) {
        for (int i = 0; i < token.size(); ++i) {
            if (token[i].isLetter()) {
                token[i] = token[i].toUpper();
                break;
            }
        }
    }
    return token;
}

static bool planningRangeInsidePlate(const QList<QPair<int, int>>& ranges, int start, int end)
{
    for (const auto& range : ranges) {
        if (start >= range.first && end <= range.second) return true;
    }
    return false;
}

QString normalizePlanningSlotText(const QString& input, bool finalPass)
{
    static const QRegularExpression masterIdRe(
        QString::fromUtf8(R"((\s*\|\s*Майстер ID:\s*[^|]+)$)"), QRegularExpression::CaseInsensitiveOption);
    const auto identity = masterIdRe.match(input);
    if (identity.hasMatch()) {
        return normalizePlanningSlotText(input.left(identity.capturedStart(1)), finalPass)
            + QString::fromUtf8(" | Майстер ID: ") + identity.captured(1).section(':', 1).trimmed();
    }
    const QString source = finalPass ? input.trimmed() : input;
    if (source.trimmed().isEmpty()) return finalPass ? QString() : source;

    const QString condensed = source.trimmed();
    if (PlateUtils::isLikelyPlateInput(condensed)) {
        QString formatted = PlateUtils::formatPlateProgressive(condensed);
        if (!finalPass && input.endsWith(QChar(' ')) && !formatted.endsWith(QChar(' '))) {
            formatted += QChar(' ');
        }
        return formatted;
    }

    // Use the same correction path as the planning editor; a second casing
    // pass used to lower acronyms and vehicle identifiers in reminder actions.
    return ManualInputNormalizer::normalize(source, ManualInputNormalizer::Role::Planning).text;

}

QTime extractReminderTime(const QString& raw, int fallbackHour)
{
    static const QRegularExpression timeRe(QStringLiteral(R"((?:^|\s)((?:[01]?\d|2[0-3]):[0-5]\d)(?=\s|$))"));
    const auto match = timeRe.match(raw);
    if (match.hasMatch()) {
        const QTime parsed = QTime::fromString(match.captured(1), QStringLiteral("H:mm"));
        if (parsed.isValid()) return parsed;
    }
    return QTime(fallbackHour, 0);
}

QString compactPlanLine(int hour, const QString& raw)
{
    QString txt = raw.trimmed();
    static const QRegularExpression masterIdRe(
        QString::fromUtf8(R"(\s*\|\s*Майстер ID:\s*[^|]+$)"), QRegularExpression::CaseInsensitiveOption);
    txt.remove(masterIdRe);
    if (txt.isEmpty()) return QString();

    QString icon;
    if (txt.contains(QString::fromUtf8("✕")) || txt.contains(QString::fromUtf8("❌"))) {
        icon = QString::fromUtf8("❌");
    } else if (txt.contains(QString::fromUtf8("✓")) || txt.contains(QString::fromUtf8("✅"))) {
        icon = QString::fromUtf8("✅");
    }

    txt.remove(QString::fromUtf8("✕"));
    txt.remove(QString::fromUtf8("❌"));
    txt.remove(QString::fromUtf8("✓"));
    txt.remove(QString::fromUtf8("✅"));
    txt = txt.simplified();

    const QTime shownTime = extractReminderTime(raw, hour);
    const QString shownPrefix = shownTime.toString(QStringLiteral("HH:mm"));
    static const QRegularExpression leadingTimeRe(QStringLiteral(R"(^\s*((?:[01]?\d|2[0-3]):[0-5]\d)\s+)"));
    const auto leading = leadingTimeRe.match(txt);
    if (leading.hasMatch()) {
        txt.remove(0, leading.capturedLength(1));
        txt = txt.trimmed();
    }

    if (txt.isEmpty()) {
        return icon.isEmpty() ? shownPrefix : QStringLiteral("%1 %2").arg(shownPrefix, icon);
    }

    if (icon.isEmpty()) {
        return QStringLiteral("%1 %2").arg(shownPrefix, txt);
    }
    return QStringLiteral("%1 %2 %3").arg(shownPrefix, icon, txt);
}

QString stripReminderMarkers(QString text)
{
    text.remove(QString::fromUtf8("✕"));
    text.remove(QString::fromUtf8("❌"));
    text.remove(QString::fromUtf8("✓"));
    text.remove(QString::fromUtf8("✅"));
    return text.simplified();
}

bool hasReminderDecisionMarker(const QString& text)
{
    return text.contains(QString::fromUtf8("❌"))
        || text.contains(QString::fromUtf8("✕"))
        || text.contains(QString::fromUtf8("✅"))
        || text.contains(QString::fromUtf8("✓"));
}

QString withReminderMarker(const QString& text, const QString& marker)
{
    const QString clean = stripReminderMarkers(text);
    if (clean.isEmpty()) return QString();
    if (marker.trimmed().isEmpty()) return clean;
    return QStringLiteral("%1 %2").arg(marker.trimmed(), clean);
}

QString extractPlanningPlateNorm(const QString& rawText)
{
    static const QRegularExpression plateRe(
        QStringLiteral(R"((?<!\S)([A-Za-zА-Яа-яІіЇїЄєҐґ]{2}[\s\-]?\d{4,5}[\s\-]?[A-Za-zА-Яа-яІіЇїЄєҐґ]{2})(?!\S))"));

    auto matchIt = plateRe.globalMatch(rawText);
    while (matchIt.hasNext()) {
        const auto match = matchIt.next();
        const QString plateNorm = PlateUtils::normalizePlate(match.captured(1));
        if (!plateNorm.isEmpty()) return plateNorm;
    }

    if (PlateUtils::isLikelyPlateInput(rawText)) {
        return PlateUtils::normalizePlate(rawText);
    }
    return QString();
}

int findLatestVehicleOrderNumber(const QString& plateNorm)
{
    return PlanningQueryService::findLatestVehicleOrderNumber(plateNorm);
}

} // namespace PlanningPageSupport
