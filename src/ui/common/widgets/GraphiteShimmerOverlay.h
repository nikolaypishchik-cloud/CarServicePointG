#pragma once

#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>
#include <QPixmap>

class GraphiteShimmerOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit GraphiteShimmerOverlay(QWidget* targetParent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void syncToParent();
    void scheduleNext();
    void startRandomPass();
    bool shimmerEnabled() const;
    void rebuildCacheIfNeeded();

    QWidget* m_targetParent = nullptr;
    QTimer m_waitTimer;
    QTimer m_frameTimer;
    QElapsedTimer m_animClock;

    QPointF m_start;
    QPointF m_end;
    QPointF m_dir;
    qreal m_bandWidth = 160.0;
    qreal m_peakAlpha = 42.0;
    int m_durationMs = 1700;
    QSize m_cachedSize;
    QPixmap m_cachedBase;
};
