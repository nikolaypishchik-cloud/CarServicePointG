#include "ui/tabs/schedule/PlanningPage.h"
#include "ui/tabs/schedule/PlanningDesk.h"
#include "core/services/planning/PlanningRepository.h"
#include "core/services/planning/PlanningQueryService.h"
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
#include <QTableWidget>
#include <QHeaderView>

void PlanningPage::setDailyView(bool daily)
{
    if (auto* button = findChild<QPushButton*>(daily ? "planningCurrentMode" : "planningWeeklyMode")) button->click();
}

void PlanningPage::refresh()
{
    if(m_desk){if(!m_editor->isVisible())m_desk->refresh();return;}
    if (!m_editor || !m_editor->isVisible()) setMonth(property("externalFitToPanel").toBool() ? QDate::currentDate() : QDate(m_year, m_month, 1));
    refreshDailyPlanning();
}

void PlanningPage::refreshDailyPlanning()
{
    if (!m_dailyPlanning || m_dailyPlanning->isHidden()) return;
    const auto today = QDate::currentDate();
    const auto entries = m_repo->visibleDayMap(today);
    m_plan[today] = entries;
    m_dailyPlanning->setHorizontalHeaderLabels({QString::fromUtf8("Час · %1").arg(today.toString("dd.MM.yy")), QString::fromUtf8("Заплановані роботи")});
    for (int hour = 8; hour <= 18; ++hour) {
        m_dailyPlanning->setItem(hour - 8, 0, new QTableWidgetItem(QString("%1:00").arg(hour, 2, 10, QChar('0'))));
        m_dailyPlanning->setItem(hour - 8, 1, new QTableWidgetItem(entries.value(hour)));
    }
    updatePlanningClock(QDateTime::currentDateTime());
}

void PlanningPage::fitDailyRows()
{
    if (!m_dailyPlanning || m_dailyPlanning->isHidden()) return;
    const int current = m_displayTime.time().hour() - 8;
    QList<int> heights;
    int required = 0;
    m_dailyPlanning->resizeRowsToContents();
    for (int row = 0; row < 11; ++row) {
        const int base = qMax(32, m_dailyPlanning->rowHeight(row));
        const int height = row == current ? qCeil(base * 1.65) : base;
        heights << height;
        required += height;
    }
    m_dailyPlanning->setProperty("dailyMinimumRowsHeight", required);
    int extra = qMax(0, m_dailyPlanning->viewport()->height() - required);
    const double weight = current >= 0 && current < 11 ? 11.65 : 11.0;
    const int space = extra;
    for (int row = 0; row < 11; ++row) {
        const int addition = row == 10 ? extra : qFloor(space * (row == current ? 1.65 : 1.0) / weight);
        extra -= addition;
        m_dailyPlanning->setRowHeight(row, heights[row] + addition);
    }
}

void PlanningPage::updatePlanningClock(const QDateTime& now)
{
    m_displayTime = now;
    for (auto* cell : m_dayCellPool) cell->setCurrentDate(cell->date() == now.date());
    if (m_grid) {
        const bool external = property("externalFitToPanel").toBool();
        for (int col = 0; col < 6; ++col) m_grid->setColumnStretch(col, 10);
        for (int row = 0; row < m_grid->rowCount(); ++row) m_grid->setRowStretch(row, external ? 0 : 10);
        if (m_bottomSpacer) m_grid->setRowStretch(m_grid->rowCount() - 1, external ? 1 : 0);
        for (int i = 0; i < m_grid->count(); ++i) {
            auto* cell = qobject_cast<PlanningDayCell*>(m_grid->itemAt(i)->widget());
            if (!cell) continue;
            int row, col, rows, cols; m_grid->getItemPosition(i, &row, &col, &rows, &cols);
            if (external) { cell->setMinimumSize(176, 150); continue; }
            if (cell->date() != now.date()) continue;
            m_grid->setColumnStretch(col, 13); m_grid->setRowStretch(row, 13);
        }
        m_grid->activate();
    }
    if (!m_dailyPlanning || m_dailyPlanning->isHidden()) return;
    const int current = now.time().hour() - 8;
    for (int row = 0; row < 11; ++row) {
        const auto* entry = m_dailyPlanning->item(row, 1);
        bool active = false;
        if (row == current && entry) {
            for (const auto& text : entry->text().split(QStringLiteral(" || "))) {
                if (text.contains(QString::fromUtf8("❌")) || text.contains(QString::fromUtf8("✕"))) continue;
                active |= PlanningQueryService::vehicleWorkInProgress(PlanningPageSupport::extractPlanningPlateNorm(text));
            }
        }
        for (int col = 0; col < 2; ++col) if (auto* item = m_dailyPlanning->item(row, col)) {
            QFont font = m_dailyPlanning->font(); font.setBold(row == current); item->setFont(font);
            item->setData(Qt::UserRole + 1, row == current);
            item->setForeground(QColor("#101418"));
            if (row == current) {
                QLinearGradient raised(0, 0, 0, 1); raised.setCoordinateMode(QGradient::ObjectBoundingMode);
                raised.setColorAt(0, active ? QColor("#e4ffe9") : QColor("#ffffff"));
                raised.setColorAt(0.15, active ? QColor("#b5ecc1") : QColor("#e4edf7"));
                raised.setColorAt(1, active ? QColor("#73cf8a") : QColor("#b4c8dc"));
                item->setBackground(QBrush(raised));
                item->setToolTip(active ? QString::fromUtf8("Зараз · наряд у роботі") : QString::fromUtf8("Поточна година"));
            } else { item->setBackground(QBrush(Qt::white)); item->setToolTip({}); }
        }
    }
    fitDailyRows();
}

void PlanningPage::resetSessionView()
{
    hidePreview();
    closeEditor(false);
    dismissActiveReminder(true);
    m_plan.clear();
    m_reminderActiveDayKeys.clear();
    m_remindedKeys.clear();
    refresh();
}
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
#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif
#if CS_HAS_MULTIMEDIA
#include <QSoundEffect>
#endif
using namespace PlanningPageSupport;
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
} // namespace
PlanningPage::PlanningPage(QWidget* parent,bool dispatch)
    : QWidget(parent)
{
    m_repo = new PlanningRepository();
    buildUi();
    m_calendarController = new PlanningCalendarController(this);
    m_editorController = new PlanningEditorController(this);
    m_reminderController = new PlanningReminderController(this);
    m_popupController = new PlanningPopupController(this);
    const QDate today = QDate::currentDate();
    setMonth(today);
    updatePlanningClock(QDateTime::currentDateTime());
    m_hoverTimer.setSingleShot(true);
    m_hoverTimer.setInterval(300);
    connect(&m_hoverTimer, &QTimer::timeout, this, [this]() {
        if (m_hoverDate.isValid()) showPreviewFor(m_hoverDate);
    });
    // reminders: independent from the visible Planning tab; lead time is stored in General settings
    m_reminderDay = QDate::currentDate();
    m_reminderTimer.setInterval(30 * 1000);
    connect(&m_reminderTimer, &QTimer::timeout, this, &PlanningPage::checkReminders);
    m_reminderTimer.start();
    if(dispatch){
        findChild<QWidget*>("planning_topbar")->hide();m_scroll->hide();
        m_desk=new PlanningDesk(this,[this](QDate date){openOnlineBookingDate(date);});
        layout()->addWidget(m_desk);
    }
}
void PlanningPage::openOnlineBookingDate(const QDate& day)
{
    if (!day.isValid()) return;
    setMonth(day);
    QPointer<PlanningPage> self(this);
    QTimer::singleShot(80, this, [self, day]() {
        if (!self) return;
        self->openEditorFor(day);
    });
}

void PlanningPage::setPageFrozen(bool frozen)
{
    if (m_pageFrozen == frozen) return;
    m_pageFrozen = frozen;
    if (frozen) {
        m_hoverTimer.stop();
        m_hoverDate = QDate();
        hidePreview();
        ++m_alignGeneration;
        if (!m_reminderTimer.isActive()) m_reminderTimer.start();
        return;
    }
    if (isVisible()) {
        if (!m_reminderTimer.isActive()) m_reminderTimer.start();
        QPointer<PlanningPage> self(this);
        QTimer::singleShot(0, this, [self]() {
            if (!self) return;
            self->checkReminders();
            self->alignCurrentWeekTop(false);
        });
    }
}

QRect PlanningPage::editorTargetRect() const
{
    const QRect area = m_scroll->viewport()->rect();
    const QSize sz(qMax(760, area.width()-60), qMax(520, area.height()-60));
    const QPoint p = m_scroll->viewport()->mapTo(const_cast<PlanningPage*>(this),
        QPoint((area.width()-sz.width())/2, (area.height()-sz.height())/2));
    return QRect(p, sz);
}
void PlanningPage::animateEditorOpen()
{
    if (!m_editor || !m_editorAnimGroup) return;
    m_editorAnimGroup->stop();
    const QRect target = editorTargetRect();
    const QPoint c = target.center();
    const QSize startSz(qMax(420, target.width()/2), qMax(280, target.height()/2));
    const QRect start(QPoint(c.x() - startSz.width()/2, c.y() - startSz.height()/2), startSz);
    m_editor->setGeometry(start);
    m_editor->show();
    m_editor->raise();
    m_editorGeomAnim->setStartValue(start);
    m_editorGeomAnim->setEndValue(target);
    m_editorOpacityAnim->setStartValue(0.0);
    m_editorOpacityAnim->setEndValue(1.0);
    m_editorAnimGroup->start();
}
void PlanningPage::animateEditorClose(bool hideOnly)
{
    if (!m_editor || !m_editorAnimGroup || !m_editorOpacity) {
        if (!hideOnly && m_editor) m_editor->hide();
        return;
    }
    if (!m_editor->isVisible()) return;
    m_editorAnimGroup->stop();
    const QRect cur = m_editor->geometry();
    const QPoint c = cur.center();
    const QSize endSz(qMax(380, cur.width()/2), qMax(240, cur.height()/2));
    const QRect end(QPoint(c.x() - endSz.width()/2, c.y() - endSz.height()/2), endSz);
    m_editorGeomAnim->setStartValue(cur);
    m_editorGeomAnim->setEndValue(end);
    m_editorOpacityAnim->setStartValue(m_editorOpacity->opacity());
    m_editorOpacityAnim->setEndValue(0.0);
    m_editorAnimGroup->start();
    if (hideOnly) {
        // nothing else
    }
}
QString reminderRuntimeKey(const QDate& day, int slotHour, const QString& rawText)
{
    const QString normalized = QStringLiteral("%1|%2|%3")
        .arg(day.toString(Qt::ISODate))
        .arg(slotHour, 2, 10, QChar('0'))
        .arg(stripReminderMarkers(rawText).trimmed().toLower());
    const QByteArray hash = QCryptographicHash::hash(normalized.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QStringLiteral("%1|%2").arg(day.toString(Qt::ISODate), QString::fromLatin1(hash));
}
void PlanningPage::setMonth(const QDate& anyDayInMonth)
{
    if (!m_calendarController) return;
    m_calendarController->setMonth(anyDayInMonth);
}
void PlanningPage::alignCurrentWeekTop(bool forceResetToTodayMonth)
{
    if (!m_calendarController) return;
    m_calendarController->alignCurrentWeekTop(forceResetToTodayMonth);
}
void PlanningPage::ensureCellPoolSize(int requiredCount)
{
    if (!m_calendarController) return;
    m_calendarController->ensureCellPoolSize(requiredCount);
}
void PlanningPage::rebuildGrid()
{
    if (!m_calendarController) return;
    m_calendarController->rebuildGrid();
}
bool PlanningPage::event(QEvent* e)
{
    // keep editor centered on resize
    if (e->type() == QEvent::Resize) {
        if (m_editor && m_editor->isVisible()) {
            // if we're mid-animation, let it finish; otherwise snap to the new target rect
            const bool animRunning = (m_editorAnimGroup && m_editorAnimGroup->state() == QAbstractAnimation::Running);
            if (!animRunning) m_editor->setGeometry(editorTargetRect());
        }
    } else if (e->type() == QEvent::WindowDeactivate || e->type() == QEvent::Hide || e->type() == QEvent::Leave) {
        m_hoverTimer.stop();
        m_hoverDate = QDate();
        hidePreview();
        // Reminder dialog is global for the application and must not be closed
        // simply because the Planning page was hidden by tab switching.
    }
    return QWidget::event(e);
}
void PlanningPage::hideEvent(QHideEvent* e)
{
    m_hoverTimer.stop();
    m_hoverDate = QDate();
    hidePreview();
    if (!m_reminderTimer.isActive()) m_reminderTimer.start();
    ++m_alignGeneration;
    QWidget::hideEvent(e);
}
void PlanningPage::showEvent(QShowEvent* e)
{
    hidePreview();
    QWidget::showEvent(e);
    if (m_pageFrozen) return;
    if (!m_reminderTimer.isActive()) m_reminderTimer.start();
    QPointer<PlanningPage> self(this);
    QTimer::singleShot(0, this, [self]() {
        if (!self) return;
        self->checkReminders();
    });
    // Keep page switches lightweight: only re-align the current week when the
    // page is really active, without forcing a month rebuild on every show.
    alignCurrentWeekTop(false);
}
bool PlanningPage::eventFilter(QObject* watched, QEvent* ev)
{
    if (m_dailyPlanning && watched == m_dailyPlanning->viewport() && ev->type() == QEvent::Resize)
        QTimer::singleShot(0, this, [this] { fitDailyRows(); });
    auto* cell = qobject_cast<PlanningDayCell*>(watched);
    if (!cell || !m_popupController) return QWidget::eventFilter(watched, ev);
    if (m_popupController->handleCellEvent(cell, ev)) return true;
    return QWidget::eventFilter(watched, ev);
}
void PlanningPage::showPreviewFor(const QDate& d)
{
    if (!m_popupController) return;
    m_popupController->showPreviewFor(d);
}
void PlanningPage::hidePreview()
{
    if (!m_popupController) return;
    m_popupController->hidePreview();
}
void PlanningPage::openEditorFor(const QDate& d)
{
    if (!m_editorController) return;
    m_editorController->openEditorFor(d);
}
void PlanningPage::closeEditor(bool save)
{
    if (!m_editorController) return;
    m_editorController->closeEditor(save);
    if(save && m_desk)m_desk->refresh();
    refreshDailyPlanning();
}
void PlanningPage::updateEditorHeader()
{
    if (!m_editorController) return;
    m_editorController->updateEditorHeader();
}
void PlanningPage::dismissActiveReminder(bool immediate)
{
    if (!m_popupController) return;
    m_popupController->dismissActiveReminder(immediate);
}
void PlanningPage::setReminderState(const QDate& day, int slotHour, const QString& rawText, const QString& marker)
{
    if (!m_reminderController) return;
    m_reminderController->setReminderState(day, slotHour, rawText, marker);
}
bool PlanningPage::moveReminderEntry(const QDate& fromDay, int fromHour, const QString& rawText, const QDate& toDay, int toHour)
{
    if (!m_reminderController) return false;
    return m_reminderController->moveReminderEntry(fromDay, fromHour, rawText, toDay, toHour);
}
void PlanningPage::showAnimatedReminder(const QDate& day, int slotHour, const QString& rawText)
{
    if (!m_reminderController) return;
    m_reminderController->showAnimatedReminder(day, slotHour, rawText);
}
void PlanningPage::checkReminders()
{
    if (!m_reminderController) return;
    m_reminderController->checkReminders();
}
