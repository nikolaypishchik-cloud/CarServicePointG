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
    enum class Phase {
        Reveal,
        Hold,
        PreDissolveSweep,
        Dissolve
    };

    void onTick();
    qreal letterOpacity(int index) const;
    qreal letterLift(int index) const;
    qreal metallicSweepFactor(const QRectF& bounds) const;
    void rebuildCacheIfNeeded();

    QString m_text;
    QTimer m_timer;
    QElapsedTimer m_clock;
    Phase m_phase = Phase::Reveal;
    qint64 m_phaseStartMs = 0;

    static constexpr int kRevealPerLetterMs = 170;
    static constexpr int kRevealSettleMs = 420;
    static constexpr int kHoldMs = 120000;
    static constexpr int kSweepMs = 2200;
    static constexpr int kDissolvePerLetterMs = 180;
    static constexpr int kDissolveTailMs = 600;

    QFont m_cachedFont;
    QSize m_cachedSize;
    QVector<QPixmap> m_letterPixmaps;
};
