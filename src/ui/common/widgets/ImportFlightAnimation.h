#pragma once

#include <QWidget>
#include <QTimer>
#include <QPointer>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QEasingCurve>
#include <QTableView>
#include <QTableWidget>
#include <QHeaderView>
#include <QAbstractAnimation>
#include <QAbstractItemModel>
#include <functional>
#include <algorithm>

class ImportFlightXlsIcon final : public QWidget
{
public:
    explicit ImportFlightXlsIcon(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF bounds = rect().adjusted(4, 4, -4, -4);

        QPainterPath shadowPath;
        shadowPath.addRoundedRect(bounds.translated(0, 5), 12, 12);
        p.fillPath(shadowPath, QColor(0, 0, 0, 48));

        const QRectF doc = bounds.adjusted(0, 0, -6, -2);
        const QRectF fold(doc.right() - doc.width() * 0.23, doc.top(), doc.width() * 0.23, doc.height() * 0.22);

        QPainterPath docPath;
        docPath.moveTo(doc.left() + 10, doc.top());
        docPath.lineTo(fold.left(), doc.top());
        docPath.lineTo(doc.right(), fold.bottom());
        docPath.lineTo(doc.right(), doc.bottom() - 10);
        docPath.quadTo(doc.right(), doc.bottom(), doc.right() - 10, doc.bottom());
        docPath.lineTo(doc.left() + 10, doc.bottom());
        docPath.quadTo(doc.left(), doc.bottom(), doc.left(), doc.bottom() - 10);
        docPath.lineTo(doc.left(), doc.top() + 10);
        docPath.quadTo(doc.left(), doc.top(), doc.left() + 10, doc.top());

        QLinearGradient grad(doc.topLeft(), doc.bottomLeft());
        grad.setColorAt(0.0, QColor(255, 255, 255, 252));
        grad.setColorAt(1.0, QColor(232, 238, 242, 248));
        p.fillPath(docPath, grad);
        p.setPen(QPen(QColor(196, 205, 212, 235), 1.2));
        p.drawPath(docPath);

        QPainterPath foldPath;
        foldPath.moveTo(fold.topLeft());
        foldPath.lineTo(fold.topRight());
        foldPath.lineTo(fold.bottomRight());
        foldPath.closeSubpath();
        p.fillPath(foldPath, QColor(217, 226, 232, 250));
        p.setPen(QPen(QColor(192, 201, 209, 235), 1.0));
        p.drawPath(foldPath);

        const QRectF band(doc.left() + 9, doc.top() + doc.height() * 0.52, doc.width() - 18, doc.height() * 0.28);
        QLinearGradient bandGrad(band.topLeft(), band.bottomLeft());
        bandGrad.setColorAt(0.0, QColor(36, 152, 84, 235));
        bandGrad.setColorAt(1.0, QColor(13, 103, 54, 245));
        p.fillRect(band, bandGrad);

        QFont f = p.font();
        f.setBold(true);
        f.setPointSizeF(qMax<qreal>(8.5, band.height() * 0.42));
        p.setFont(f);
        p.setPen(QColor(255, 255, 255, 245));
        p.drawText(band, Qt::AlignCenter, QStringLiteral("XLS"));

        QPen linePen(QColor(178, 188, 198, 210), 1.0);
        p.setPen(linePen);
        const qreal lineLeft = doc.left() + 11;
        const qreal lineRight = doc.right() - 12;
        for (int i = 0; i < 3; ++i) {
            const qreal y = doc.top() + doc.height() * (0.22 + 0.11 * i);
            p.drawLine(QPointF(lineLeft, y), QPointF(lineRight, y));
        }
    }
};

class ImportFlightTargetPulse final : public QWidget
{
public:
    explicit ImportFlightTargetPulse(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = rect().adjusted(6, 6, -6, -6);
        const QPointF c = r.center();
        const qreal radius = qMax(r.width(), r.height()) * 0.46;
        QRadialGradient halo(c, radius);
        halo.setColorAt(0.0, QColor(140, 195, 255, 110));
        halo.setColorAt(0.55, QColor(104, 170, 245, 60));
        halo.setColorAt(1.0, QColor(80, 120, 190, 0));
        p.setBrush(halo);
        p.setPen(Qt::NoPen);
        p.drawEllipse(c, radius, radius * 0.78);

        QPen pen(QColor(222, 240, 255, 175), 2.0);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r, 9, 9);
    }
};

class ImportFlightAnimation final
{
public:
    using RectProvider = std::function<QRect()>;

    class LoopHandle final : public QObject
    {
    public:
        LoopHandle(QWidget* context,
                   RectProvider sourceProvider,
                   RectProvider targetProvider,
                   int cycleMs,
                   int durationMs)
            : QObject(context)
            , m_context(context)
            , m_sourceProvider(std::move(sourceProvider))
            , m_targetProvider(std::move(targetProvider))
            , m_cycleMs(qMax(450, cycleMs))
            , m_durationMs(qMax(380, durationMs))
        {
            m_cycleTimer.setInterval(m_cycleMs);
            connect(&m_cycleTimer, &QTimer::timeout, this, [this]() { playCycle(); });
        }

        void start()
        {
            if (m_running) return;
            m_running = true;
            playCycle();
            m_cycleTimer.start();
        }

        void stop(bool allowLastCycle = true)
        {
            m_running = false;
            m_cycleTimer.stop();
            if (!allowLastCycle || m_activeCycles <= 0) {
                deleteLater();
            }
        }

    private:
        void playCycle()
        {
            if (!m_context) return;
            QWidget* host = m_context->window();
            if (!host) host = m_context;
            if (!host || !host->isVisible()) return;

            QRect targetRect = normalizeRect(m_targetProvider ? m_targetProvider() : QRect());
            if (!targetRect.isValid()) return;
            QRect startRect = normalizeRect(m_sourceProvider ? m_sourceProvider() : QRect());
            if (!startRect.isValid()) {
                startRect = defaultBottomSourceRect(host, targetRect, 76);
            }

            auto* overlay = new QWidget(host);
            overlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            overlay->setAttribute(Qt::WA_NoSystemBackground, true);
            overlay->setAttribute(Qt::WA_TranslucentBackground, true);
            overlay->setStyleSheet(QStringLiteral("background: transparent;"));
            overlay->setGeometry(host->rect());
            overlay->show();
            overlay->raise();

            auto* icon = new ImportFlightXlsIcon(overlay);
            icon->setGeometry(startRect);
            icon->show();
            icon->raise();

            auto* iconOpacity = new QGraphicsOpacityEffect(icon);
            iconOpacity->setOpacity(0.0);
            icon->setGraphicsEffect(iconOpacity);

            auto* pulse = new ImportFlightTargetPulse(overlay);
            pulse->setGeometry(targetRect.adjusted(-22, -16, 22, 16));
            pulse->show();
            pulse->raise();
            auto* pulseOpacity = new QGraphicsOpacityEffect(pulse);
            pulseOpacity->setOpacity(0.0);
            pulse->setGraphicsEffect(pulseOpacity);

            auto* geomAnim = new QPropertyAnimation(icon, "geometry", overlay);
            geomAnim->setDuration(m_durationMs);
            geomAnim->setStartValue(startRect);
            geomAnim->setEndValue(shrinkIntoTargetRect(targetRect));
            geomAnim->setEasingCurve(QEasingCurve::InOutCubic);

            auto* opacityAnim = new QPropertyAnimation(iconOpacity, "opacity", overlay);
            opacityAnim->setDuration(m_durationMs);
            opacityAnim->setKeyValueAt(0.0, 0.0);
            opacityAnim->setKeyValueAt(0.08, 1.0);
            opacityAnim->setKeyValueAt(0.84, 1.0);
            opacityAnim->setKeyValueAt(1.0, 0.0);
            opacityAnim->setEasingCurve(QEasingCurve::InOutQuad);

            const QRect pulseStartRect = targetRect.adjusted(-12, -8, 12, 8);
            const QRect pulseEndRect = targetRect.adjusted(-28, -22, 28, 22);
            pulse->setGeometry(pulseStartRect);

            auto* pulseGeomAnim = new QPropertyAnimation(pulse, "geometry", overlay);
            pulseGeomAnim->setDuration(qMax(260, m_durationMs - 120));
            pulseGeomAnim->setKeyValueAt(0.0, pulseStartRect);
            pulseGeomAnim->setKeyValueAt(0.72, pulseStartRect);
            pulseGeomAnim->setKeyValueAt(1.0, pulseEndRect);
            pulseGeomAnim->setEasingCurve(QEasingCurve::OutCubic);

            auto* pulseOpacityAnim = new QPropertyAnimation(pulseOpacity, "opacity", overlay);
            pulseOpacityAnim->setDuration(qMax(260, m_durationMs - 120));
            pulseOpacityAnim->setKeyValueAt(0.0, 0.0);
            pulseOpacityAnim->setKeyValueAt(0.72, 0.0);
            pulseOpacityAnim->setKeyValueAt(0.82, 0.95);
            pulseOpacityAnim->setEndValue(0.0);
            pulseOpacityAnim->setEasingCurve(QEasingCurve::OutQuad);

            auto* group = new QParallelAnimationGroup(overlay);
            group->addAnimation(geomAnim);
            group->addAnimation(opacityAnim);
            group->addAnimation(pulseGeomAnim);
            group->addAnimation(pulseOpacityAnim);

            ++m_activeCycles;
            const QPointer<LoopHandle> self(this);
            connect(group, &QParallelAnimationGroup::finished, overlay, [overlay, self]() {
                overlay->deleteLater();
                if (!self) return;
                self->m_activeCycles = qMax(0, self->m_activeCycles - 1);
                if (!self->m_running && self->m_activeCycles == 0) {
                    self->deleteLater();
                }
            });
            group->start(QAbstractAnimation::DeleteWhenStopped);
        }

        QPointer<QWidget> m_context;
        RectProvider m_sourceProvider;
        RectProvider m_targetProvider;
        QTimer m_cycleTimer;
        int m_cycleMs = 1250;
        int m_durationMs = 840;
        int m_activeCycles = 0;
        bool m_running = false;
    };

    static LoopHandle* startLoop(QWidget* contextWidget,
                                 const RectProvider& targetProvider,
                                 int cycleMs = 1250,
                                 int durationMs = 840)
    {
        if (!contextWidget || !targetProvider) return nullptr;
        auto* handle = new LoopHandle(contextWidget,
                                      [contextWidget, targetProvider]() {
                                          QWidget* host = contextWidget->window();
                                          if (!host) host = contextWidget;
                                          return defaultBottomSourceRect(host, normalizeRect(targetProvider()), 76);
                                      },
                                      targetProvider,
                                      cycleMs,
                                      durationMs);
        handle->start();
        return handle;
    }

    static LoopHandle* startLoop(QWidget* contextWidget,
                                 const RectProvider& sourceProvider,
                                 const RectProvider& targetProvider,
                                 int cycleMs,
                                 int durationMs = 840)
    {
        if (!contextWidget || !targetProvider) return nullptr;
        auto* handle = new LoopHandle(contextWidget,
                                      sourceProvider,
                                      targetProvider,
                                      cycleMs,
                                      durationMs);
        handle->start();
        return handle;
    }

    static void play(QWidget* contextWidget,
                     QWidget* targetWidget,
                     const std::function<void()>& afterFlight,
                     int durationMs = 700)
    {
        if (!contextWidget || !targetWidget) {
            if (afterFlight) afterFlight();
            return;
        }
        play(contextWidget,
             [contextWidget, targetWidget]() { return widgetRectInHost(contextWidget, targetWidget); },
             afterFlight,
             durationMs);
    }

    static void play(QWidget* contextWidget,
                     const RectProvider& targetProvider,
                     const std::function<void()>& afterFlight,
                     int durationMs = 700)
    {
        if (!contextWidget || !targetProvider) {
            if (afterFlight) afterFlight();
            return;
        }
        auto* handle = startLoop(contextWidget, targetProvider, qMax(durationMs + 120, 9000), durationMs);
        if (!handle) {
            if (afterFlight) afterFlight();
            return;
        }
        QPointer<LoopHandle> safeHandle(handle);
        QWidget* callbackHost = contextWidget;
        QTimer::singleShot(qMax(360, durationMs + 20), contextWidget, [safeHandle, callbackHost, afterFlight]() {
            if (safeHandle) safeHandle->stop(true);
            if (!afterFlight) return;
            if (callbackHost) {
                QTimer::singleShot(0, callbackHost, [afterFlight]() { afterFlight(); });
            } else {
                afterFlight();
            }
        });
    }

    static QRect widgetRectInHost(QWidget* contextWidget, QWidget* targetWidget)
    {
        if (!contextWidget || !targetWidget || !targetWidget->isVisible()) return {};
        QWidget* host = contextWidget->window();
        if (!host) host = contextWidget;
        const QPoint topLeft = targetWidget->mapTo(host, QPoint(0, 0));
        return QRect(topLeft, targetWidget->size());
    }

    static QRect tableViewCellRect(QWidget* contextWidget, QTableView* table, int row, int column)
    {
        if (!contextWidget || !table || !table->model() || column < 0) return {};
        QWidget* host = contextWidget->window();
        if (!host) host = contextWidget;
        if (row >= 0 && column < table->model()->columnCount()) {
            const QModelIndex idx = table->model()->index(row, column);
            if (idx.isValid()) {
                const QRect visual = table->visualRect(idx);
                if (visual.isValid()) {
                    const QPoint mapped = table->viewport()->mapTo(host, visual.topLeft());
                    return QRect(mapped, visual.size());
                }
            }
        }
        return tableViewColumnHintRect(contextWidget, table, column);
    }

    static QRect tableViewColumnHintRect(QWidget* contextWidget, QTableView* table, int column)
    {
        if (!contextWidget || !table || !table->model() || column < 0 || column >= table->model()->columnCount()) return {};
        QWidget* host = contextWidget->window();
        if (!host) host = contextWidget;
        const int x = table->columnViewportPosition(column);
        const int width = table->columnWidth(column);
        if (width <= 0) return {};
        QRect body(x, qMax(0, table->horizontalHeader()->height() + 6), width, qMax(42, table->viewport()->height() / 5));
        body.moveTop(qMax(8, qMin(body.top(), table->viewport()->height() - body.height() - 8)));
        const QPoint mapped = table->viewport()->mapTo(host, body.topLeft());
        return QRect(mapped, body.size());
    }

    static QRect tableWidgetCellRect(QWidget* contextWidget, QTableWidget* table, int row, int column)
    {
        if (!contextWidget || !table || column < 0) return {};
        QWidget* host = contextWidget->window();
        if (!host) host = contextWidget;
        if (row >= 0 && column < table->columnCount()) {
            if (QTableWidgetItem* item = table->item(row, column)) {
                const QRect visual = table->visualItemRect(item);
                if (visual.isValid()) {
                    const QPoint mapped = table->viewport()->mapTo(host, visual.topLeft());
                    return QRect(mapped, visual.size());
                }
            }
        }
        return tableWidgetColumnHintRect(contextWidget, table, column);
    }

    static QRect tableWidgetColumnHintRect(QWidget* contextWidget, QTableWidget* table, int column)
    {
        if (!contextWidget || !table || column < 0 || column >= table->columnCount()) return {};
        QWidget* host = contextWidget->window();
        if (!host) host = contextWidget;
        const int x = table->columnViewportPosition(column);
        const int width = table->columnWidth(column);
        if (width <= 0) return {};
        QRect body(x, qMax(0, table->horizontalHeader()->height() + 6), width, qMax(42, table->viewport()->height() / 5));
        body.moveTop(qMax(8, qMin(body.top(), table->viewport()->height() - body.height() - 8)));
        const QPoint mapped = table->viewport()->mapTo(host, body.topLeft());
        return QRect(mapped, body.size());
    }

    static QRect defaultBottomSourceRect(QWidget* host, const QRect& targetRect, int size = 76)
    {
        if (!host) return {};
        const int dpiY = qRound(host->logicalDpiY() > 0 ? host->logicalDpiY() : 96.0);
        const int offset = qMax(120, qRound(dpiY * 1.95));
        const int hostWidth = host->width();
        const int hostHeight = host->height();
        int centerX = targetRect.isValid() ? targetRect.center().x() : hostWidth / 2;
        centerX = qBound(size / 2 + 12, centerX, hostWidth - size / 2 - 12);
        int centerY = qMax(size / 2 + 12, hostHeight - offset);
        if (targetRect.isValid()) {
            centerY = qMax(centerY, targetRect.bottom() + size / 2 + 18);
            centerY = qMin(hostHeight - size / 2 - 12, centerY);
        }
        return QRect(centerX - size / 2, centerY - size / 2, size, size);
    }

private:
    static QRect normalizeRect(const QRect& rect)
    {
        if (!rect.isValid()) return {};
        const int w = qMax(24, rect.width());
        const int h = qMax(18, rect.height());
        return QRect(rect.topLeft(), QSize(w, h));
    }

    static QRect shrinkIntoTargetRect(const QRect& targetRect)
    {
        if (!targetRect.isValid()) return {};
        const QSize endSize(qMax(24, qMin(42, targetRect.width() / 2 + 14)), qMax(18, qMin(32, targetRect.height() / 2 + 12)));
        const QPoint c = targetRect.center();
        return QRect(c.x() - endSize.width() / 2, c.y() - endSize.height() / 2, endSize.width(), endSize.height());
    }
};
