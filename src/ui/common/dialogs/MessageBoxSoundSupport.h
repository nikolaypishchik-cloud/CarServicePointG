#include <QtGlobal>

#pragma once

namespace MessageBoxSoundSupport {

qint64 nowMs();
bool soundsEnabled();
int soundsVolumePct();

bool playAlert(int volumePct);
bool playClose(int volumePct);
bool playConfirmationShow(int volumePct);
bool playConfirmationYes(int volumePct);
bool playConfirmationNo(int volumePct);
bool playSettingsAction(int volumePct);
void primeSettingsAction();

} // namespace MessageBoxSoundSupport
