#include "ui/tabs/schedule/PlanningEditorController.h"

#include "ui/tabs/schedule/PlanningPage.h"
#include "ui/tabs/schedule/PlanningPageSupport.h"
#include "ui/tabs/schedule/widgets/PlanningDayCell.h"
#include "core/services/planning/PlanningRepository.h"
#include "core/services/AccessControlService.h"
#include <QMessageBox>

#include <QDate>
#include <QStringList>
#include <QLabel>
#include <QLineEdit>
#include <QGraphicsOpacityEffect>
#include <QParallelAnimationGroup>

using namespace PlanningPageSupport;

PlanningEditorController::PlanningEditorController(PlanningPage* page)
    : m_page(page)
{
}

void PlanningEditorController::openEditorFor(const QDate& date)
{
    if (!m_page || !date.isValid()) return;
    m_page->m_hoverTimer.stop();
    m_page->m_hoverDate = QDate();
    m_page->hidePreview();
    m_page->m_editDate = date;
    for (QObject* obj : m_page->m_gridHost->children()) {
        if (auto* w = qobject_cast<PlanningDayCell*>(obj)) {
            w->setSelected(w->date() == date);
        }
    }
    const auto it = m_page->m_plan.constFind(date);
    for (auto& r : m_page->m_rows) r.edit->clear();
    if (it != m_page->m_plan.cend()) {
        for (auto v = it->cbegin(); v != it->cend(); ++v) {
            const int hour = v.key();
            for (auto& r : m_page->m_rows) {
                if (r.timeLabel->text().startsWith(QString("%1").arg(hour, 2, 10, QChar('0')))) {
                    r.edit->setText(v.value());
                    break;
                }
            }
        }
    }
    updateEditorHeader();
    m_page->animateEditorOpen();
    if (!m_page->m_rows.isEmpty()) m_page->m_rows.first().edit->setFocus(Qt::TabFocusReason);
    m_page->hidePreview();
}

void PlanningEditorController::closeEditor(bool save)
{
    if (!m_page || !m_page->m_editor || !m_page->m_editor->isVisible()) return;
    if (!save) {
        if (m_page->m_editorAnimGroup) m_page->m_editorAnimGroup->stop();
        if (m_page->m_editorOpacity) m_page->m_editorOpacity->setOpacity(0.0);
        m_page->m_editor->hide();
        m_page->m_editDate = QDate();
        return;
    }
    if (m_page->m_editDate.isValid()) {
        QMap<int, QString> day = m_page->m_plan.value(m_page->m_editDate);
        for (auto& r : m_page->m_rows) {
            const QString t = r.timeLabel->text();
            const int hour = t.left(2).toInt();
            day.remove(hour);
            const QString txt = r.edit->text().trimmed();
            if (!txt.isEmpty()) day[hour] = txt;
        }
        if (AccessControlService::currentActorIsMaster() && m_page->m_repo) {
            const auto all = m_page->m_repo->getDayMap(m_page->m_editDate);
            const auto own = m_page->m_repo->visibleDayMap(m_page->m_editDate);
            for (auto it = day.cbegin(); it != day.cend(); ++it) {
                if (all.contains(it.key()) && !own.contains(it.key())) {
                    QMessageBox::warning(m_page, QString::fromUtf8("Планування"), QString::fromUtf8("Цей час зайнятий. Оберіть інший час."));
                    return;
                }
            }
        }
        if (day.isEmpty()) m_page->m_plan.remove(m_page->m_editDate);
        else m_page->m_plan[m_page->m_editDate] = day;
    }
    if (m_page->m_repo && m_page->m_editDate.isValid()) {
        m_page->m_repo->setVisibleDayMap(m_page->m_editDate, m_page->m_plan.value(m_page->m_editDate));
        m_page->m_plan[m_page->m_editDate] = m_page->m_repo->visibleDayMap(m_page->m_editDate);
    }
    if (m_page->m_editDate.isValid() && m_page->m_reminderDay == m_page->m_editDate) {
        const QString prefix = QStringLiteral("%1|").arg(m_page->m_editDate.toString(Qt::ISODate));
        for (auto it = m_page->m_remindedKeys.begin(); it != m_page->m_remindedKeys.end(); ) {
            if (it->startsWith(prefix)) it = m_page->m_remindedKeys.erase(it);
            else ++it;
        }
    }
    m_page->animateEditorClose(false);
    m_page->m_editDate = QDate();
    for (QObject* obj : m_page->m_gridHost->children()) {
        auto* w = qobject_cast<PlanningDayCell*>(obj);
        if (!w) continue;
        const QDate cd = w->date();
        const auto it = m_page->m_plan.constFind(cd);
        if (it == m_page->m_plan.cend() || it->isEmpty()) {
            w->setPreviewText(QString());
        } else {
            QStringList lines;
            for (auto v = it->cbegin(); v != it->cend(); ++v) {
                const QString line = compactPlanLine(v.key(), v.value());
                if (!line.isEmpty()) lines << line;
                if (lines.size() >= 3) break;
            }
            w->setPreviewText(lines.join("\n"));
        }
        w->setCurrentDate(w->date() == QDate::currentDate());
    }
}

void PlanningEditorController::updateEditorHeader()
{
    if (!m_page || !m_page->m_editDate.isValid()) return;
    m_page->m_editorDate->setText(QString("%1.%2.%3")
        .arg(m_page->m_editDate.day(), 2, 10, QChar('0'))
        .arg(m_page->m_editDate.month(), 2, 10, QChar('0'))
        .arg(m_page->m_editDate.year() % 100, 2, 10, QChar('0')));
    const auto it = m_page->m_plan.constFind(m_page->m_editDate);
    bool has = false;
    if (it != m_page->m_plan.cend()) {
        for (auto v = it->cbegin(); v != it->cend(); ++v) {
            if (!v.value().trimmed().isEmpty()) {
                has = true;
                break;
            }
        }
    }
    m_page->m_editorInfo->setText(has ? QString::fromUtf8("Є заплановані роботи") : QString::fromUtf8("Запланованих робіт нема"));
}
