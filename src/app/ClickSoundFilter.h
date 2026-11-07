#pragma once

#include <QObject>
#include <QtGlobal>

class QEvent;

class ClickSoundFilter : public QObject
{
    Q_OBJECT
public:
    explicit ClickSoundFilter(QObject* parent = nullptr);
    void reload();
    static void playGlobalClick();
    static void playGlobalTableClick();
    static void playGlobalMinimize();
    static void playGlobalRestore();
    static void playGlobalStartup();
    static void playGlobalClose();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool shouldPlayFor(QObject* watched, QEvent* event) const;
    bool shouldPlayTableSoundFor(QObject* watched, QEvent* event) const;
    bool shouldPlayMinimizeSoundFor(QObject* watched, QEvent* event) const;
    bool shouldPlayRestoreSoundFor(QObject* watched, QEvent* event) const;
};
