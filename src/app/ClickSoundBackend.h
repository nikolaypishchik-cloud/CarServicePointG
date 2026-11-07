#pragma once

#include <QtGlobal>

class QWidget;

namespace ClickSoundBackend {

qint64 nowMs();
bool clickSoundsEnabled();
int clickSoundsVolumePct();
bool isInsideTableView(QWidget* widget);
void reloadEffects();
void preloadStartupSoundAssets();
bool playClick(int volumePct);
bool playTableClick(int volumePct);
bool playMinimize(int volumePct);
bool playRestore(int volumePct);
bool playStartup(int volumePct);
bool playClose(int volumePct);

} // namespace ClickSoundBackend
