#include "ui/tabs/schedule/PlanningPopupController.h"

#include "ui/tabs/schedule/PlanningPage.h"
#include "ui/tabs/schedule/PlanningPageSupport.h"
#include "ui/tabs/schedule/widgets/PlanningDayCell.h"

#include <QApplication>
#include <QLabel>
#include <QScrollArea>
#include <QDialog>
#include <QWindow>
#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QMouseEvent>
#include <QEvent>

using namespace PlanningPageSupport;

PlanningPopupController::PlanningPopupController(PlanningPage* page)
    : m_page(page)
{
}

void PlanningPopupController::showPreviewFor(const QDate& date)
{
    if (!m_page || !date.isValid()) return;
    if (!m_page->isVisible() || !m_page->window() || !m_page->window()->isActiveWindow()) {
        hidePreview();
        return;
    }
    if (QApplication::activeModalWidget()) {
        hidePreview();
        return;
    }
    if (m_page->m_editor && m_page->m_editor->isVisible()) {
        hidePreview();
        return;
    }
    if (m_page->m_preview && m_page->m_preview->parentWidget() != m_page->window()) {
        m_page->m_preview->setParent(m_page->window(), Qt::Tool | Qt::FramelessWindowHint);
    }
    PlanningDayCell* anchorCell = nullptr;
    for (QObject* obj : m_page->m_gridHost->children()) {
        if (auto* w = qobject_cast<PlanningDayCell*>(obj)) {
            if (w->date() == date) {
                anchorCell = w;
                break;
            }
        }
    }
    if (!anchorCell || !anchorCell->isVisible()) {
        hidePreview();
        return;
    }
    const QPoint gp = QCursor::pos();
    const QRect globalRect(anchorCell->mapToGlobal(QPoint(0, 0)), anchorCell->size());
    if (!globalRect.adjusted(-2, -2, 2, 2).contains(gp)) {
        hidePreview();
        return;
    }
    m_page->m_previewTitle->setText(QString("%1.%2.%3 - %4")
        .arg(date.day(), 2, 10, QChar('0'))
        .arg(date.month(), 2, 10, QChar('0'))
        .arg(date.year() % 100, 2, 10, QChar('0'))
        .arg(uaWeekday(date.dayOfWeek())));
    const auto it = m_page->m_plan.constFind(date);
    if (it == m_page->m_plan.cend() || it->isEmpty()) {
        m_page->m_previewBody->setText(QString::fromUtf8("Запланованих робіт нема"));
    } else {
        QStringList lines;
        for (auto v = it->cbegin(); v != it->cend(); ++v) {
            const QString line = compactPlanLine(v.key(), v.value());
            if (line.isEmpty()) continue;
            lines << line;
            if (lines.size() >= 3) break;
        }
        m_page->m_previewBody->setText(lines.isEmpty() ? QString::fromUtf8("Запланованих робіт нема") : lines.join("\n"));
    }
    m_page->m_preview->adjustSize();
    QPoint pos;
    bool anchored = false;
    if (anchorCell) {
        const QRect cellRect = anchorCell->rect();
        const QPoint globalBottomLeft = anchorCell->mapToGlobal(QPoint(8, cellRect.height() + 10));
        pos = QPoint(globalBottomLeft.x(), globalBottomLeft.y());
        anchored = true;
    }
    if (!anchored) {
        const QPoint fallback = m_page->m_scroll->viewport()->mapToGlobal(QPoint(24, 24));
        pos = QPoint(fallback.x(), fallback.y());
    }
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen && m_page->window() && m_page->window()->windowHandle()) screen = m_page->window()->windowHandle()->screen();
    if (!screen) screen = QApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
    pos.setX(qBound(avail.left() + 8, pos.x(), avail.right() - m_page->m_preview->width() - 8));
    pos.setY(qBound(avail.top() + 8, pos.y(), avail.bottom() - m_page->m_preview->height() - 8));
    m_page->m_preview->move(pos);
    m_page->m_preview->show();
    m_page->m_preview->raise();
}

void PlanningPopupController::hidePreview()
{
    if (m_page && m_page->m_preview && m_page->m_preview->isVisible()) m_page->m_preview->hide();
}

void PlanningPopupController::dismissActiveReminder(bool immediate)
{
    if (!m_page || !m_page->m_activeReminderDialog) return;
    auto* dialog = m_page->m_activeReminderDialog.data();
    m_page->m_activeReminderDialog.clear();
    if (!dialog) return;
    if (immediate) {
        dialog->hide();
        dialog->close();
        return;
    }
    dialog->hide();
    dialog->close();
}


bool PlanningPopupController::handleCellEvent(PlanningDayCell* cell, QEvent* event)
{
    if (!m_page || !cell || !event) return false;
    if (m_page->m_editor && m_page->m_editor->isVisible()) {
        if (event->type() == QEvent::Enter || event->type() == QEvent::Leave) {
            m_page->m_hoverTimer.stop();
            m_page->m_hoverDate = QDate();
            hidePreview();
        }
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                m_page->openEditorFor(cell->date());
                return true;
            }
        }
        return false;
    }
    if (event->type() == QEvent::Enter) {
        m_page->m_hoverDate = cell->date();
        m_page->m_hoverTimer.start();
        return false;
    }
    if (event->type() == QEvent::Leave) {
        const QPoint gp = QCursor::pos();
        const QRect globalRect(cell->mapToGlobal(QPoint(0,0)), cell->size());
        if (!globalRect.adjusted(-2,-2,2,2).contains(gp)) {
            m_page->m_hoverTimer.stop();
            m_page->m_hoverDate = QDate();
            QTimer::singleShot(40, m_page, [page=m_page, cell]() {
                const QRect checkRect(cell->mapToGlobal(QPoint(0,0)), cell->size());
                if (!checkRect.adjusted(-2,-2,2,2).contains(QCursor::pos())) page->hidePreview();
            });
        }
        return false;
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            m_page->m_hoverTimer.stop();
            hidePreview();
            m_page->openEditorFor(cell->date());
            return true;
        }
    }
    return false;
}
