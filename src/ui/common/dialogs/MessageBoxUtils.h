#pragma once

class QMessageBox;

void applyStandardMessageBoxStyle(QMessageBox* box);
void playStandardMessageBoxAlertSound(QMessageBox* box);
void playStandardMessageBoxConfirmationShowSound(QMessageBox* box);
void installStandardMessageBoxConfirmationYesSoundHook(QMessageBox* box);
void installStandardMessageBoxConfirmationNoSoundHook(QMessageBox* box);
void installStandardMessageBoxSettingsActionSoundHook(QMessageBox* box);
void playStandardMessageBoxCloseSound(QMessageBox* box);

inline void styleMessageBoxReadable(QMessageBox* box)
{
    applyStandardMessageBoxStyle(box);
}
