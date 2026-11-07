#include "core/util/DateFormats.h"
#include "ui/tabs/schedule/PlanningCalendarController.h"

#include "ui/tabs/schedule/PlanningPage.h"
#include "ui/tabs/schedule/PlanningPageSupport.h"
#include "ui/tabs/schedule/widgets/PlanningDayCell.h"
#include "core/services/planning/PlanningRepository.h"

#include <QGridLayout>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QLayoutItem>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QTimer>
#include <QWidget>
#include <QDate>
#include <QStringList>
#include <algorithm>
#include <utility>

using namespace PlanningPageSupport;

namespace {
// Reserve the same grid slot for every day; today's card fills the slot,
// while other cards are inset. No overlapping proxy-widget effects are needed.
class ExternalDayItem final : public QWidgetItem {
public:
    explicit ExternalDayItem(PlanningDayCell* cell) : QWidgetItem(cell), m_cell(cell) {}
    QSize minimumSize() const override {
        const QSize size = QWidgetItem::minimumSize();
        return QSize(qCeil(size.width() * 1.12), qCeil(size.height() * 1.12));
    }
    QSize sizeHint() const override { return QWidgetItem::sizeHint().expandedTo(minimumSize()); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override {
        const int cardWidth = qFloor(width / 1.12);
        return qCeil(qMax(m_cell->minimumHeight(), m_cell->heightForWidth(cardWidth)) * 1.12);
    }
    int minimumHeightForWidth(int width) const override { return heightForWidth(width); }
    void setGeometry(const QRect& rect) override {
        QRect card = rect;
        if (m_cell->date() != QDate::currentDate()) {
            const QSize size(qRound(rect.width() / 1.12), qRound(rect.height() / 1.12));
            card.setSize(size); card.moveCenter(rect.center());
        }
        QWidgetItem::setGeometry(card);
    }
private:
    PlanningDayCell* m_cell;
};
}

PlanningCalendarController::PlanningCalendarController(PlanningPage* page)
    : m_page(page)
{
}

void PlanningCalendarController::setMonth(const QDate& anyDayInMonth)
{
    if (!m_page || !anyDayInMonth.isValid()) return;
    m_page->m_year = anyDayInMonth.year();
    m_page->m_month = anyDayInMonth.month();
    if (m_page->m_lblMonthTitle) {
        m_page->m_lblMonthTitle->setText(QString::fromUtf8("%1 %2").arg(uaMonth(m_page->m_month)).arg(m_page->m_year));
    }
    const bool b1 = m_page->m_cbMonth->blockSignals(true);
    const bool b2 = m_page->m_spYear->blockSignals(true);
    const int idx = m_page->m_cbMonth->findData(m_page->m_month);
    if (idx >= 0) m_page->m_cbMonth->setCurrentIndex(idx);
    m_page->m_spYear->setValue(m_page->m_year);
    m_page->m_cbMonth->blockSignals(b1);
    m_page->m_spYear->blockSignals(b2);
    if (m_page->m_repo) {
        QList<QDate> toRemove;
        for (auto it = m_page->m_plan.cbegin(); it != m_page->m_plan.cend(); ++it) {
            if (it.key().year() == m_page->m_year && it.key().month() == m_page->m_month) toRemove.push_back(it.key());
        }
        for (const QDate& d : toRemove) m_page->m_plan.remove(d);
        const auto monthMaps = m_page->m_repo->visibleMonthMaps(m_page->m_year, m_page->m_month);
        for (auto it = monthMaps.cbegin(); it != monthMaps.cend(); ++it) {
            m_page->m_plan[it.key()] = it.value();
        }

        QList<QString> activeKeysToRemove;
        for (const QString& key : std::as_const(m_page->m_reminderActiveDayKeys)) {
            const QDate d = DateFormats::parseDate(key);
            if (d.isValid() && d.year() == m_page->m_year && d.month() == m_page->m_month) {
                activeKeysToRemove.push_back(key);
            }
        }
        for (const QString& key : activeKeysToRemove) m_page->m_reminderActiveDayKeys.remove(key);
        const QSet<QString> activeKeys = m_page->m_repo->getMonthReminderActiveDayKeys(m_page->m_year, m_page->m_month);
        for (const QString& key : activeKeys) m_page->m_reminderActiveDayKeys.insert(key);
    }
    rebuildGrid();
    if (QDate::currentDate().year() == m_page->m_year && QDate::currentDate().month() == m_page->m_month) {
        alignCurrentWeekTop(false);
    }
}

void PlanningCalendarController::alignCurrentWeekTop(bool forceResetToTodayMonth)
{
    if (!m_page) return;
    const QDate today = QDate::currentDate();
    if (!today.isValid()) return;
    const int generation = ++m_page->m_alignGeneration;
    auto alignOnce = [this, today, generation]() {
        if (!m_page || generation != m_page->m_alignGeneration) return;
        if (!m_page->isVisible() || !m_page->m_scroll || !m_page->m_scroll->verticalScrollBar() || !m_page->m_gridHost || !m_page->m_grid) return;
        if (m_page->m_year != today.year() || m_page->m_month != today.month()) return;
        if (m_page->m_gridHost->layout()) m_page->m_gridHost->layout()->activate();
        m_page->m_gridHost->adjustSize();
        if (m_page->m_scroll->widget()) m_page->m_scroll->widget()->adjustSize();
        if (m_page->m_scroll->viewport()) m_page->m_scroll->viewport()->update();
        const QDate weekStart = today.addDays(1 - today.dayOfWeek());
        const QDate weekEnd = weekStart.addDays(5);
        int targetY = -1;
        for (int i = 0; i < m_page->m_grid->count(); ++i) {
            QLayoutItem* item = m_page->m_grid->itemAt(i);
            if (!item) continue;
            auto* w = qobject_cast<PlanningDayCell*>(item->widget());
            if (!w) continue;
            const QDate d = w->date();
            if (d >= weekStart && d <= weekEnd) {
                const int top = w->geometry().top();
                targetY = (targetY < 0) ? top : qMin(targetY, top);
            }
        }
        if (targetY >= 0) m_page->m_scroll->verticalScrollBar()->setValue(targetY);
    };
    if (forceResetToTodayMonth && (m_page->m_year != today.year() || m_page->m_month != today.month())) {
        setMonth(today);
    }
    QTimer::singleShot(0, m_page, alignOnce);
    QTimer::singleShot(40, m_page, alignOnce);
    QTimer::singleShot(120, m_page, alignOnce);
}

void PlanningCalendarController::ensureCellPoolSize(int requiredCount)
{
    if (!m_page) return;
    while (m_page->m_dayCellPool.size() < requiredCount) {
        auto* cell = new PlanningDayCell(m_page->m_gridHost);
        cell->hide();
        cell->installEventFilter(m_page);
        QObject::connect(cell, &PlanningDayCell::reminderBellToggled, m_page, [this, cell](const QDate& day, bool active) {
            if (!m_page || !day.isValid()) return;
            const QString key = day.toString(Qt::ISODate);
            if (active) m_page->m_reminderActiveDayKeys.insert(key);
            else m_page->m_reminderActiveDayKeys.remove(key);
            if (m_page->m_repo) m_page->m_repo->setDayReminderActive(day, active);
            cell->setReminderActive(active);

            const QString dayPrefix = QStringLiteral("%1|").arg(key);
            for (auto itKey = m_page->m_remindedKeys.begin(); itKey != m_page->m_remindedKeys.end(); ) {
                if (itKey->startsWith(dayPrefix)) itKey = m_page->m_remindedKeys.erase(itKey);
                else ++itKey;
            }
            if (!active && day == QDate::currentDate()) {
                m_page->dismissActiveReminder(true);
            } else if (active && day == QDate::currentDate()) {
                QPointer<PlanningPage> safePage(m_page);
                QTimer::singleShot(0, m_page, [safePage]() {
                    if (safePage) safePage->checkReminders();
                });
            }
        });
        m_page->m_dayCellPool.push_back(cell);
    }
}

void PlanningCalendarController::rebuildGrid()
{
    if (!m_page || !m_page->m_grid) return;
    while (QLayoutItem* it = m_page->m_grid->takeAt(0)) {
        if (auto* w = it->widget()) w->hide();
        delete it;
    }
    m_page->hidePreview();
    m_page->closeEditor(false);
    const QDate first(m_page->m_year, m_page->m_month, 1);
    QDate start = first;
    while (start.dayOfWeek() != 1) start = start.addDays(1);
    const bool external = m_page->property("externalFitToPanel").toBool();
    const QDate today = QDate::currentDate();
    if (external && first.year() == today.year() && first.month() == today.month()) {
        start = today.addDays(1 - today.dayOfWeek());
        // The current week may start in the previous month.
        if (m_page->m_repo && start.month() != today.month()) {
            const auto preceding = m_page->m_repo->visibleMonthMaps(start.year(), start.month());
            for (auto it = preceding.cbegin(); it != preceding.cend(); ++it) m_page->m_plan[it.key()] = it.value();
        }
    }
    QDate last(m_page->m_year, m_page->m_month, first.daysInMonth());
    while (last.dayOfWeek() != 6) last = last.addDays(1);
    int visibleCells = 0;
    for (QDate d = start; d <= last; d = d.addDays(1)) {
        if (d.dayOfWeek() != 7) ++visibleCells;
    }
    ensureCellPoolSize(visibleCells);
    int row = 0;
    int cellIndex = 0;
    for (QDate d = start; d <= last; d = d.addDays(1)) {
        if (d.dayOfWeek() == 7) continue;
        const int col = d.dayOfWeek() - 1;
        if (col == 0 && d != start) row++;
        auto* cell = m_page->m_dayCellPool.at(cellIndex++);
        cell->setParent(m_page->m_gridHost);
        cell->setDate(d);
        cell->setSelected(false);
        const auto it = m_page->m_plan.constFind(d);
        if (it == m_page->m_plan.cend() || it->isEmpty()) {
            cell->setPreviewText(QString());
        } else {
            QStringList lines;
            for (auto v = it->cbegin(); v != it->cend(); ++v) {
                const QString line = compactPlanLine(v.key(), v.value());
                if (!line.isEmpty()) lines << line;
                if (lines.size() >= 3) break;
            }
            cell->setPreviewText(lines.join("\n"));
        }
        cell->setReminderActive(m_page->m_reminderActiveDayKeys.contains(d.toString(Qt::ISODate)));
        cell->setCurrentDate(d == QDate::currentDate());
        if (external) cell->setMinimumSize(176, 150);
        cell->show();
        if (external) m_page->m_grid->addItem(new ExternalDayItem(cell), row, col);
        else m_page->m_grid->addWidget(cell, row, col);
    }
    for (int i = cellIndex; i < m_page->m_dayCellPool.size(); ++i) {
        auto* cell = m_page->m_dayCellPool.at(i);
        cell->resetVisualState();
        cell->hide();
    }
    if (!m_page->m_bottomSpacer) {
        m_page->m_bottomSpacer = new QWidget(m_page->m_gridHost);
        m_page->m_bottomSpacer->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_page->m_bottomSpacer->setStyleSheet("background: transparent; border: none;");
    }
    const int spacerH = m_page->property("externalFitToPanel").toBool() ? 0
        : m_page->m_scroll && m_page->m_scroll->viewport() ? qMax(260, m_page->m_scroll->viewport()->height() - 40) : 420;
    m_page->m_bottomSpacer->setMinimumHeight(spacerH);
    m_page->m_bottomSpacer->setMaximumHeight(external ? QWIDGETSIZE_MAX : spacerH);
    m_page->m_bottomSpacer->show();
    m_page->m_grid->addWidget(m_page->m_bottomSpacer, row + 1, 0, 1, 6);
    m_page->updatePlanningClock(QDateTime::currentDateTime());
}
