#pragma once

#include <QWidget>
#include <QVector>
#include <QPixmap>
#include <QTimer>
#include <QElapsedTimer>
#include <QString>
#include <QHash>

class SplashScreen : public QWidget
{
    Q_OBJECT
public:
    explicit SplashScreen(QWidget* parent=nullptr);

    // Ring playback duration; handoff follows the last painted title letter.
    void start(int durationMs = 5500);
    void setStartupReady();

signals:
    void finished();

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    void buildFrameManifest();
    QPixmap sourceFrameForIndex(int idx) const;
    QPixmap scaledFrameForIndex(int idx, int targetSize) const;

    QVector<QString> m_framePaths;
    mutable QHash<int, QPixmap> m_sourceFrameCache;
    mutable QHash<QString, QPixmap> m_scaledFrameCache;
    QTimer m_tick;
    QElapsedTimer m_elapsed;
    int m_durationMs = 5500;
    QString m_title;
    int m_lastWallElapsedMs = 0;
    int m_animElapsedMs = 0;
    bool m_startupReady = false;
    int m_titleCompleteWallMs = -1;
    bool m_loggedFirstPaint = false;
    bool m_loggedTextStart = false;
    bool m_loggedTitleComplete = false;
};
