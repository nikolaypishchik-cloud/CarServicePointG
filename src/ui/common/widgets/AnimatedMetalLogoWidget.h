#pragma once

#include <QWidget>
#include <QElapsedTimer>
#include <QTimer>
#include <QPixmap>
#include <QVector>

class AnimatedMetalLogoWidget : public QWidget {
    Q_OBJECT
public:
    explicit AnimatedMetalLogoWidget(QWidget* parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void onTick();
    qreal metallicSweepFactor() const;
    void rebuildCacheIfNeeded();

    QString m_text;
    QTimer m_timer;
    QElapsedTimer m_clock;
    bool m_sweeping = false;
    static constexpr int kHoldMs = 120000;
    static constexpr int kSweepMs = 2200;

    QFont m_cachedFont;
    QSize m_cachedSize;
    QVector<QPixmap> m_letterPixmaps;
};
