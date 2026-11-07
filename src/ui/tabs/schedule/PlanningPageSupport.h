#pragma once

#include <QByteArray>
#include <QDate>
#include <QTime>
#include <QString>
#include <QWidget>

namespace PlanningPageSupport {

void forceReminderWindowOnTop(QWidget* widget);

bool reminderSoundsEnabled();
int reminderSoundsVolumePct();
QByteArray loadReminderKnockBytes(QString* loadedFrom = nullptr);
void playReminderKnockSound();

QString uaWeekday(int dow);
QString uaMonth(int month);

bool isPlanningUpperAbbreviation(const QString& token);
QString planningSentenceCaseToken(QString token, bool capitalizeFirstLetter);
QString normalizePlanningSlotText(const QString& input, bool finalPass = true);
QTime extractReminderTime(const QString& raw, int fallbackHour);
QString compactPlanLine(int hour, const QString& raw);
QString stripReminderMarkers(QString text);
bool hasReminderDecisionMarker(const QString& text);
QString withReminderMarker(const QString& text, const QString& marker);
QString extractPlanningPlateNorm(const QString& rawText);
int findLatestVehicleOrderNumber(const QString& plateNorm);

} // namespace PlanningPageSupport
