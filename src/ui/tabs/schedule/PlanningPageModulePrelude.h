#pragma once

// P2 architecture module prelude.
// Generated during monolith-removal p2 so converted implementation slices
// compile as separate .cpp modules without being text-included by the owner.
// Owner: src/ui/tabs/schedule/PlanningPage.cpp

#include "ui/tabs/schedule/PlanningPage.h"
#include "core/services/planning/PlanningRepository.h"
#include "ui/tabs/schedule/PlanningPageSupport.h"
#include "ui/tabs/schedule/PlanningCalendarController.h"
#include "ui/tabs/schedule/PlanningEditorController.h"
#include "ui/tabs/schedule/PlanningReminderController.h"
#include "ui/tabs/schedule/PlanningPopupController.h"
#include "ui/tabs/schedule/widgets/PlanningDayCell.h"
#include "ui/common/dialogs/OrderDialog.h"
#include "ui/main/MainWindow.h"
#include "app/AppContext.h"
#include "core/backup/BackupPerformanceGuard.h"
#include "core/util/PlateUtils.h"
#include "core/util/VehicleNameUtils.h"
#include "core/util/SoundSupport.h"
#include "ui/common/AdaptiveUi.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QFrame>
#include <QLineEdit>
#include <QDate>
#include <QTime>
#include <QDateTime>
#include <QMessageBox>
#include <QMenu>
#include <QDialog>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QTimeEdit>
#include <QPainter>
#include <QPainterPath>
#include <functional>
#include <QSet>
#include <QEvent>
#include <QCloseEvent>
#include <QMouseEvent>
#include <QApplication>
#include <QCoreApplication>
#include <QScreen>
#include <QWindow>
#include <QSpacerItem>
#include <QGraphicsOpacityEffect>
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QEasingCurve>
#include <QAbstractAnimation>
#include <algorithm>
#include <QRegularExpression>
#include <QUrl>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QRegion>
#include <QTimer>
#include <QPointer>
#include <QWheelEvent>
#include <QSqlError>
#include <thread>
#include <string>
#include <windows.h>
#include <mmsystem.h>
#if CS_HAS_MULTIMEDIA
#include <QSoundEffect>
#endif

using namespace PlanningPageSupport;

// File-local helpers required by converted slices.
namespace {
class VerticalWheelHintPopup final : public QWidget
{
public:
    explicit VerticalWheelHintPopup(QWidget* parent=nullptr)
        : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
    {
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(14, 12, 14, 12);
        lay->setSpacing(1);
        auto makeLabel = [lay](int pt, int alpha, QWidget* parent) {
            auto* lbl = new QLabel(parent);
            lbl->setAlignment(Qt::AlignHCenter);
            lbl->setStyleSheet(QStringLiteral("background: transparent; color: rgba(245,248,252,%1); font-size:%2pt; font-weight:700;")
                               .arg(alpha).arg(pt));
            lay->addWidget(lbl);
            return lbl;
        };
        m_prev = makeLabel(11, 125, this);
        m_curr = makeLabel(16, 255, this);
        m_next = makeLabel(11, 125, this);
        resize(108, 124);
    }
    void setTexts(const QString& prev, const QString& curr, const QString& next)
    {
        m_prev->setText(prev);
        m_curr->setText(curr);
        m_next->setText(next);
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF shadowRect = rect().adjusted(10, 12, -10, -6);
        QPainterPath shadowPath;
        shadowPath.addRoundedRect(shadowRect, 20, 20);
        p.fillPath(shadowPath, QColor(8, 14, 22, 72));
        const QRectF r = rect().adjusted(3, 3, -3, -6);
        QLinearGradient glass(r.topLeft(), r.bottomRight());
        glass.setColorAt(0.0, QColor(20, 27, 35, 210));
        glass.setColorAt(0.32, QColor(68, 80, 92, 175));
        glass.setColorAt(0.55, QColor(138, 149, 160, 120));
        glass.setColorAt(0.80, QColor(48, 58, 68, 190));
        glass.setColorAt(1.0, QColor(18, 24, 30, 220));
        QPainterPath path;
        path.addRoundedRect(r, 22, 22);
        p.fillPath(path, glass);
        p.setPen(QPen(QColor(255,255,255,72), 1.0));
        p.drawPath(path);
        QRectF hi = r.adjusted(8, 6, -8, -r.height()*0.56);
        QPainterPath hiPath;
        hiPath.addRoundedRect(hi, 16, 16);
        QLinearGradient shine(hi.topLeft(), hi.bottomRight());
        shine.setColorAt(0.0, QColor(255,255,255,85));
        shine.setColorAt(1.0, QColor(255,255,255,6));
        p.fillPath(hiPath, shine);
        p.setPen(QPen(QColor(255,255,255,25), 1.0));
        p.drawLine(QPointF(r.left()+16, r.center().y()), QPointF(r.right()-16, r.center().y()));
    }
private:
    QLabel* m_prev{};
    QLabel* m_curr{};
    QLabel* m_next{};
};
class VerticalWheelHintFilter final : public QObject
{
public:
    explicit VerticalWheelHintFilter(QWidget* host)
        : QObject(host), m_host(host) {}
protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Wheel) {
            if (auto* dt = qobject_cast<QDateTimeEdit*>(watched)) {
                const int delta = static_cast<QWheelEvent*>(event)->angleDelta().y();
                QPointer<QDateTimeEdit> safe = dt;
                QTimer::singleShot(0, this, [this, safe, delta]() {
                    if (!safe) return;
                    showFor(safe, delta);
                });
            }
        }
        return QObject::eventFilter(watched, event);
    }
private:
    void showFor(QDateTimeEdit* edit, int delta)
    {
        if (!edit || !edit->isVisible()) return;
        if (!m_popup) m_popup = new VerticalWheelHintPopup();
        const QString fmt = edit->displayFormat().isEmpty() ? QStringLiteral("dd.MM.yy") : edit->displayFormat();
        QString prevText, currText, nextText;
        if (qobject_cast<QDateEdit*>(edit) && !qobject_cast<QTimeEdit*>(edit)) {
            const QDate c = edit->date();
            prevText = c.addDays(-1).toString(fmt);
            currText = c.toString(fmt);
            nextText = c.addDays(1).toString(fmt);
        } else {
            const QTime c = edit->time();
            prevText = c.addSecs(-60).toString(fmt);
            currText = c.toString(fmt);
            nextText = c.addSecs(60).toString(fmt);
        }
        m_popup->setTexts(prevText, currText, nextText);
        const QRect globalEdit(edit->mapToGlobal(QPoint(0, 0)), edit->size());
        const QRect fieldRect = editFieldGlobalRect(edit);
        const QRect sectionRect = popupSectionRect(edit, fieldRect);
        const int popupGap = 44; // ~1.1cm at standard desktop DPI so the wheel does not cover the active value
        const bool preferLeftSide = sectionRect.center().x() <= fieldRect.center().x();
        int x = preferLeftSide
            ? (sectionRect.left() - m_popup->width() - popupGap)
            : (sectionRect.right() + popupGap);
        if (preferLeftSide && x < fieldRect.left() + 4) {
            x = sectionRect.right() + popupGap;
        } else if (!preferLeftSide && x + m_popup->width() > fieldRect.right() - 4) {
            x = sectionRect.left() - m_popup->width() - popupGap;
        }
        int y = globalEdit.center().y() - m_popup->height() / 2;
        if (QScreen* screen = edit->screen()) {
            const QRect avail = screen->availableGeometry();
            x = qBound(avail.left() + 6, x, avail.right() - m_popup->width() - 6);
            y = qBound(avail.top() + 6, y, avail.bottom() - m_popup->height() - 6);
        }
        const QRect endRect(QPoint(x, y), m_popup->size());
        const QRect startRect = endRect.translated(0, delta >= 0 ? 18 : -18);
        m_popup->setGeometry(startRect);
        m_popup->show();
        m_popup->raise();
        auto* eff = new QGraphicsOpacityEffect(m_popup);
        eff->setOpacity(0.0);
        m_popup->setGraphicsEffect(eff);
        auto* posAnim = new QPropertyAnimation(m_popup, "geometry", m_popup);
        posAnim->setDuration(180);
        posAnim->setStartValue(startRect);
        posAnim->setEndValue(endRect);
        posAnim->setEasingCurve(QEasingCurve::OutCubic);
        auto* opIn = new QPropertyAnimation(eff, "opacity", m_popup);
        opIn->setDuration(120);
        opIn->setStartValue(0.0);
        opIn->setEndValue(1.0);
        auto* group = new QParallelAnimationGroup(m_popup);
        group->addAnimation(posAnim);
        group->addAnimation(opIn);
        group->start(QAbstractAnimation::DeleteWhenStopped);
        QPointer<VerticalWheelHintFilter> self(this);
        QTimer::singleShot(2000, this, [self]() {
            if (!self) return;
            if (!self->m_popup) return;
            auto* effect = qobject_cast<QGraphicsOpacityEffect*>(self->m_popup->graphicsEffect());
            if (!effect) {
                self->m_popup->hide();
                return;
            }
            auto* fade = new QPropertyAnimation(effect, "opacity", self->m_popup);
            fade->setDuration(180);
            fade->setStartValue(effect->opacity());
            fade->setEndValue(0.0);
            connect(fade, &QPropertyAnimation::finished, self->m_popup, &QWidget::hide);
            fade->start(QAbstractAnimation::DeleteWhenStopped);
        });
    }
    QRect editFieldGlobalRect(QDateTimeEdit* edit) const
    {
        const QRect fieldRect = edit->rect().adjusted(12, 8, -12, -8);
        return QRect(edit->mapToGlobal(fieldRect.topLeft()), fieldRect.size());
    }
    QRect popupSectionRect(QDateTimeEdit* edit, const QRect& fieldRect) const
    {
        const QString displayed = edit->text();
        const QString fmt = edit->displayFormat();
        const QFontMetrics fm(edit->font());
        const int textWidth = fm.horizontalAdvance(displayed);
        const int textStartX = fieldRect.left() + qMax(0, (fieldRect.width() - textWidth) / 2);
        int sectionStart = 0;
        int sectionLength = displayed.size();
        if (qobject_cast<QDateEdit*>(edit) && !qobject_cast<QTimeEdit*>(edit) && (fmt == QStringLiteral("dd.MM.yy") || fmt == QStringLiteral("dd.MM.yyyy"))) {
            switch (edit->currentSection()) {
            case QDateTimeEdit::DaySection:
                sectionStart = 0; sectionLength = 2; break;
            case QDateTimeEdit::MonthSection:
                sectionStart = 3; sectionLength = 2; break;
            case QDateTimeEdit::YearSection:
                sectionStart = 6; sectionLength = fmt.endsWith("yyyy") ? 4 : 2; break;
            default:
                break;
            }
        } else if (qobject_cast<QTimeEdit*>(edit) && fmt == QStringLiteral("HH:mm")) {
            switch (edit->currentSection()) {
            case QDateTimeEdit::HourSection:
                sectionStart = 0; sectionLength = 2; break;
            case QDateTimeEdit::MinuteSection:
                sectionStart = 3; sectionLength = 2; break;
            default:
                break;
            }
        } else {
            const QString selected = edit->sectionText(edit->currentSection());
            const int idx = !selected.isEmpty() ? displayed.indexOf(selected) : -1;
            if (idx >= 0) {
                sectionStart = idx;
                sectionLength = selected.size();
            }
        }
        const int prefixWidth = fm.horizontalAdvance(displayed.left(sectionStart));
        const int sectionWidth = qMax(1, fm.horizontalAdvance(displayed.mid(sectionStart, sectionLength)));
        return QRect(textStartX + prefixWidth, fieldRect.top(), sectionWidth, fieldRect.height());
    }
    QWidget* m_host{};
    QPointer<VerticalWheelHintPopup> m_popup;
};
}
