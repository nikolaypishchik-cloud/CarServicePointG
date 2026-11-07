#pragma once

#include <QElapsedTimer>
#include <QPainter>
#include <QTimer>
#include <QWidget>
#include <cmath>

class InvoiceImportScannerOverlay final : public QWidget {
public:
    explicit InvoiceImportScannerOverlay(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        m_tick.setInterval(16);
        QObject::connect(&m_tick, &QTimer::timeout, this, [this]() { updateFrame(); });
        hide();
    }

    void setDocumentLabel(const QString& value)
    {
        m_documentLabel = value.trimmed();
        if (m_documentLabel.isEmpty()) m_documentLabel = QStringLiteral("PDF/XLS");
        update();
    }

    void setImportProgress(int percent, const QString& stage, const QString& message)
    {
        m_externalProgress = qBound(0, percent, 100);
        m_progressStage = stage.trimmed();
        m_progressMessage = message.trimmed();
        update();
    }

    void start(const QRect& bounds, int maxRuntimeMs = 0)
    {
        if (!parentWidget()) return;
        m_maxRuntimeMs = (maxRuntimeMs > 0) ? qMax(m_cycleMs, maxRuntimeMs) : 0;
        setGeometry(bounds);
        raise();
        show();
        m_elapsed.restart();
        m_running = true;
        m_tick.start();
        update();
    }

    void stop()
    {
        m_running = false;
        m_tick.stop();
        hide();
    }

    bool isRunning() const { return m_running; }
    qint64 elapsedMs() const { return m_elapsed.isValid() ? m_elapsed.elapsed() : 0; }
    int remainingForMinimumVisibleMs(int minimumVisibleMs = 2500) const
    {
        if (!m_running || minimumVisibleMs <= 0) return 0;
        return qMax(0, minimumVisibleMs - static_cast<int>(elapsedMs()));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (!m_running) return;

        const double t = progress();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);

        p.fillRect(rect(), QColor(9, 16, 28, 138));

        const QRectF canvas = rect().adjusted(22, 18, -22, -18);
        qreal gap = qBound<qreal>(14.0, canvas.width() * 0.024, 24.0);
        qreal docW = qBound<qreal>(150.0, canvas.width() * 0.24, 235.0);
        qreal flowW = qBound<qreal>(96.0, canvas.width() * 0.16, 150.0);
        const qreal minTargetW = qBound<qreal>(220.0, canvas.width() * 0.34, 360.0);

        auto remainingTargetWidth = [&]() -> qreal {
            return canvas.width() - docW - flowW - gap * 2.0;
        };

        if (remainingTargetWidth() < minTargetW) {
            qreal shortage = minTargetW - remainingTargetWidth();
            const qreal docSlack = qMax<qreal>(0.0, docW - 126.0);
            const qreal takeFromDoc = qMin(docSlack, shortage * 0.7);
            docW -= takeFromDoc;
            shortage -= takeFromDoc;

            const qreal flowSlack = qMax<qreal>(0.0, flowW - 72.0);
            const qreal takeFromFlow = qMin(flowSlack, shortage);
            flowW -= takeFromFlow;
            shortage -= takeFromFlow;

            if (shortage > 0.0) {
                const qreal gapSlack = qMax<qreal>(0.0, gap - 10.0);
                const qreal takeFromGap = qMin(gapSlack, shortage / 2.0);
                gap -= takeFromGap;
            }
        }

        const qreal docHeight = qMax<qreal>(126.0, canvas.height() * 0.58);
        const QRectF docRect(canvas.left() + 12,
                             canvas.top() + qMax<qreal>(18.0, (canvas.height() - docHeight) * 0.42),
                             docW,
                             qMin<qreal>(docHeight, canvas.height() - 18.0));
        const QRectF flowRect(docRect.right() + gap,
                              docRect.center().y() - 28,
                              flowW,
                              56.0);
        const qreal targetLeft = flowRect.right() + gap;
        const qreal targetWidth = qMax<qreal>(120.0, canvas.right() - targetLeft);
        const QRectF targetRect(targetLeft,
                                canvas.top() + 8,
                                targetWidth,
                                qMax<qreal>(140.0, canvas.height() - 16.0));

        drawDocumentPanel(p, docRect, t);
        drawScanner(p, docRect, t);
        drawFlow(p, flowRect, targetRect, t);
        drawTargetTable(p, targetRect, t);
    }

private:
    QTimer m_tick;
    QElapsedTimer m_elapsed;
    QString m_documentLabel;
    QString m_progressStage;
    QString m_progressMessage;
    int m_externalProgress = -1;
    int m_cycleMs = 2200;
    int m_maxRuntimeMs = 0;
    bool m_running = false;

    static QColor neonCyan(int alpha = 255) { return QColor(0, 229, 255, alpha); }
    static QColor neonBlue(int alpha = 255) { return QColor(75, 160, 255, alpha); }

    double progress() const
    {
        if (!m_running || m_cycleMs <= 0) return 0.0;
        const qint64 elapsedMs = qMax<qint64>(0, m_elapsed.elapsed());
        const int localMs = static_cast<int>(elapsedMs % qMax(1, m_cycleMs));
        return qBound(0.0, static_cast<double>(localMs) / static_cast<double>(m_cycleMs), 1.0);
    }

    void updateFrame()
    {
        if (!m_running) return;
        if (m_maxRuntimeMs > 0 && m_elapsed.elapsed() >= m_maxRuntimeMs) {
            stop();
            return;
        }
        update();
    }

    void drawDocumentPanel(QPainter& p, const QRectF& r, double t)
    {
        const double appear = qBound(0.0, t / 0.18, 1.0);
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(18, 28, 42, qRound(220 * appear)));
        p.drawRoundedRect(r, 18, 18);

        QLinearGradient glow(r.topLeft(), r.bottomRight());
        glow.setColorAt(0.0, QColor(62, 95, 128, qRound(120 * appear)));
        glow.setColorAt(1.0, QColor(10, 16, 28, qRound(210 * appear)));
        p.setBrush(glow);
        p.drawRoundedRect(r.adjusted(2, 2, -2, -2), 16, 16);

        p.setPen(QPen(QColor(215, 235, 255, qRound(170 * appear)), 1.2));
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 18, 18);

        const QRectF titleBand(r.left() + 16, r.top() + 14, r.width() - 32, 22);
        p.setBrush(QColor(0, 229, 255, qRound(44 * appear)));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(titleBand, 8, 8);

        QFont f = p.font();
        f.setBold(true);
        f.setPointSizeF(qMax(9.0, f.pointSizeF()));
        p.setFont(f);
        p.setPen(QColor(236, 247, 255, qRound(255 * appear)));
        p.drawText(titleBand.adjusted(10, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   QString::fromUtf8("Сканування накладної"));

        f.setBold(false);
        f.setPointSizeF(qMax(8.0, f.pointSizeF() - 0.5));
        p.setFont(f);
        const QRectF labelRect(r.left() + 16, r.bottom() - 60, r.width() - 32, 18);
        p.setPen(QColor(208, 226, 245, qRound(230 * appear)));
        p.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter,
                   m_documentLabel.isEmpty() ? QStringLiteral("PDF/XLS") : m_documentLabel);

        const QString progressText = !m_progressMessage.isEmpty()
            ? m_progressMessage
            : (!m_progressStage.isEmpty() ? m_progressStage : QString::fromUtf8("Підготовка імпорту..."));
        const QRectF statusRect(r.left() + 16, r.bottom() - 39, r.width() - 32, 18);
        p.setPen(QColor(185, 213, 236, qRound(210 * appear)));
        p.drawText(statusRect, Qt::AlignLeft | Qt::AlignVCenter, progressText);

        const int externalProgress = (m_externalProgress >= 0) ? m_externalProgress : qRound(t * 100.0);
        const QRectF barBg(r.left() + 16, r.bottom() - 14, r.width() - 32, 5);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(82, 107, 135, qRound(125 * appear)));
        p.drawRoundedRect(barBg, 2.5, 2.5);
        QRectF barFill = barBg;
        barFill.setWidth(barBg.width() * qBound(0.0, externalProgress / 100.0, 1.0));
        p.setBrush(QColor(0, 229, 255, qRound(180 * appear)));
        p.drawRoundedRect(barFill, 2.5, 2.5);

        for (int i = 0; i < 4; ++i) {
            const qreal y = r.top() + 54 + i * 23;
            const qreal w = (i == 0 ? 0.76 : (i == 1 ? 0.58 : (i == 2 ? 0.68 : 0.48))) * (r.width() - 32);
            QRectF line(r.left() + 16, y, w, 8);
            p.setBrush(QColor(202, 223, 245, qRound((i < 2 ? 86 : 64) * appear)));
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(line, 4, 4);
        }
        p.restore();
    }

    void drawScanner(QPainter& p, const QRectF& r, double t)
    {
        const double local = qBound(0.0, (t - 0.08) / 0.56, 1.0);
        const qreal y = r.top() + local * r.height();
        p.save();
        QRectF glowRect(r.left() - 4, y - 12, r.width() + 8, 24);
        QLinearGradient glow(glowRect.left(), 0, glowRect.right(), 0);
        glow.setColorAt(0.0, QColor(0, 229, 255, 0));
        glow.setColorAt(0.5, QColor(0, 229, 255, qRound(70 * (1.0 - 0.1 * local))));
        glow.setColorAt(1.0, QColor(0, 229, 255, 0));
        p.fillRect(glowRect, glow);

        p.setPen(QPen(neonCyan(qRound(230 * (0.35 + 0.65 * (1.0 - local * 0.25)))), 2.2));
        p.drawLine(QPointF(r.left() + 10, y), QPointF(r.right() - 10, y));
        p.restore();
    }

    void drawFlow(QPainter& p, const QRectF& flowRect, const QRectF& targetRect, double t)
    {
        const double local = qBound(0.0, (t - 0.18) / 0.72, 1.0);
        const qreal midY = flowRect.center().y();
        const QPointF start(flowRect.left(), midY);
        const QPointF end(targetRect.left() + 10, midY);

        p.save();
        QPen pathPen(QColor(80, 128, 188, 86), 2.0);
        p.setPen(pathPen);
        p.drawLine(start, end);

        const int packets = 7;
        for (int i = 0; i < packets; ++i) {
            const double shift = std::fmod(local * 1.4 + (static_cast<double>(i) / packets), 1.0);
            const qreal x = start.x() + (end.x() - start.x()) * shift;
            const qreal s = (i % 2 == 0) ? 8.0 : 6.0;
            QRectF packet(x - s / 2.0, midY - s / 2.0, s, s);
            QRadialGradient rg(packet.center(), s * 1.7);
            rg.setColorAt(0.0, neonCyan(245));
            rg.setColorAt(0.5, neonBlue(170));
            rg.setColorAt(1.0, QColor(0, 229, 255, 0));
            p.setBrush(rg);
            p.setPen(Qt::NoPen);
            p.drawEllipse(packet);
        }

        const qreal pulseX = start.x() + (end.x() - start.x()) * local;
        p.setPen(QPen(neonCyan(180), 1.6));
        p.drawLine(QPointF(start.x(), midY), QPointF(pulseX, midY));
        p.restore();
    }

    void drawTargetTable(QPainter& p, const QRectF& r, double t)
    {
        const double appear = qBound(0.0, (t - 0.24) / 0.22, 1.0);
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(14, 22, 34, qRound(170 * appear)));
        p.drawRoundedRect(r, 14, 14);

        const QRectF header(r.left() + 10, r.top() + 10, r.width() - 20, 28);
        p.setBrush(QColor(58, 78, 104, qRound(218 * appear)));
        p.drawRoundedRect(header, 10, 10);

        const int rows = 5;
        const qreal rowTop = header.bottom() + 10;
        const qreal rowH = qMax<qreal>(24.0, (r.bottom() - rowTop - 12) / rows);
        for (int i = 0; i < rows; ++i) {
            const double rowProgress = qBound(0.0, (t - (0.34 + i * 0.08)) / 0.16, 1.0);
            const QRectF rowRect(r.left() + 10,
                                 rowTop + i * rowH,
                                 r.width() - 20,
                                 rowH - 6);
            p.setBrush(QColor(232, 240, 249, qRound(170 * rowProgress)));
            p.drawRoundedRect(rowRect, 8, 8);

            const QRectF accent(rowRect.left() + 8, rowRect.top() + 6, qMax<qreal>(10.0, rowRect.width() * 0.22 * rowProgress), rowRect.height() - 12);
            p.setBrush(QColor(0, 229, 255, qRound(58 * rowProgress)));
            p.drawRoundedRect(accent, 5, 5);
        }

        if (t > 0.82) {
            const double done = qBound(0.0, (t - 0.82) / 0.18, 1.0);
            const QRectF badge(r.right() - 132, r.bottom() - 40, 118, 26);
            p.setBrush(QColor(32, 130, 88, qRound(220 * done)));
            p.setPen(QPen(QColor(194, 255, 226, qRound(225 * done)), 1.0));
            p.drawRoundedRect(badge, 12, 12);
            QFont f = p.font();
            f.setBold(true);
            p.setFont(f);
            p.setPen(QColor(238, 255, 246, qRound(255 * done)));
            p.drawText(badge, Qt::AlignCenter, QString::fromUtf8("Готово"));
        }
        p.restore();
    }
};
