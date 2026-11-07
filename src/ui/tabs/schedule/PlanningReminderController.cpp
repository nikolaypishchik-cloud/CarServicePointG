#include "ui/tabs/schedule/PlanningReminderController.h"

#include "ui/tabs/schedule/PlanningPage.h"
#include "ui/tabs/schedule/PlanningPageSupport.h"
#include "core/services/planning/PlanningRepository.h"
#include "core/services/settings/GeneralSettingsService.h"

#include "ui/common/dialogs/OrderDialog.h"
#include "ui/main/MainWindow.h"
#include "app/AppContext.h"
#include "core/backup/BackupPerformanceGuard.h"
#include "ui/common/AdaptiveUi.h"

#include <QDialog>
#include <functional>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDateEdit>
#include <QTimeEdit>
#include <QTimer>
#include <QPointer>
#include <QRegularExpression>
#include <QLabel>
#include <QFrame>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QEasingCurve>
#include <QAbstractAnimation>

#include <QApplication>
#include <QDateTime>
#include <QCryptographicHash>
#include <algorithm>

namespace {
QString reminderRuntimeKey(const QDate& day, int slotHour, const QString& rawText)
{
    const QString normalized = QStringLiteral("%1|%2|%3")
        .arg(day.toString(Qt::ISODate))
        .arg(slotHour, 2, 10, QChar('0'))
        .arg(PlanningPageSupport::stripReminderMarkers(rawText).trimmed().toLower());
    const QByteArray hash = QCryptographicHash::hash(normalized.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QStringLiteral("%1|%2").arg(day.toString(Qt::ISODate), QString::fromLatin1(hash));
}
}

using namespace PlanningPageSupport;

namespace {
class ScrollReminderDialog final : public QDialog
{
public:
    explicit ScrollReminderDialog(const QString& planText, QWidget* parent=nullptr)
        : QDialog(parent), m_planText(planText)
    {
        setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setModal(false);
        setWindowModality(Qt::NonModal);
        setAttribute(Qt::WA_DeleteOnClose, true);
        setAttribute(Qt::WA_StyledBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        resize(830, 315);
        setStyleSheet("background: transparent;");
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(14, 14, 14, 16);
        root->setSpacing(0);
        m_content = new QFrame(this);
        m_content->setObjectName("reminder_content");
        m_content->setAttribute(Qt::WA_StyledBackground, true);
        m_content->setStyleSheet(R"QSS(
            #reminder_content { background: transparent; border: none; border-radius: 48px; }
            QLabel#reminder_title { background: transparent; color: rgba(246,249,252,235); font-size: 21pt; font-weight: 800; }
            QLabel#reminder_text { background: transparent; color: rgba(252,254,255,245); font-size: 16pt; font-weight: 700; }
            QPushButton { min-width: 160px; min-height: 58px; border-radius: 18px; padding: 8px 18px; font-size: 14pt; font-weight: 800; color: rgba(248,250,252,240); background: rgba(18,24,30,160); border: 1px solid rgba(255,255,255,70); }
            QPushButton:hover { background: rgba(42,50,60,190); }
        )QSS");
        root->addWidget(m_content);
        auto* lay = new QVBoxLayout(m_content);
        lay->setContentsMargins(36, 28, 36, 26);
        lay->setSpacing(18);
        auto* title = new QLabel(QString::fromUtf8("Нагадування"), m_content);
        title->setObjectName("reminder_title");
        lay->addWidget(title, 0, Qt::AlignHCenter);
        auto* textLabel = new QLabel(m_planText, m_content);
        textLabel->setObjectName("reminder_text");
        textLabel->setWordWrap(true);
        textLabel->setAlignment(Qt::AlignCenter);
        lay->addWidget(textLabel);
        auto* buttons = new QHBoxLayout();
        buttons->setSpacing(12);
        auto makeBtn = [&](const QString& t){ auto* b = new QPushButton(t, m_content); buttons->addWidget(b); return b; };
        auto* btnMove = makeBtn(QString::fromUtf8("Перенести"));
        auto* btnCancel = makeBtn(QString::fromUtf8("Скасувати"));
        auto* btnConfirm = makeBtn(QString::fromUtf8("Створити наряд"));
        lay->addLayout(buttons);
        QObject::connect(btnCancel, &QPushButton::clicked, this, [this]() { if (m_onCancel) m_onCancel(); closeAnimated(); });
        QObject::connect(btnConfirm, &QPushButton::clicked, this, [this]() { if (m_onConfirm) m_onConfirm(); });
        QObject::connect(btnMove, &QPushButton::clicked, this, [this]() { if (m_onMove) m_onMove(); });
    }
    void setOnCancel(std::function<void()> fn) { m_onCancel = std::move(fn); }
    void setOnConfirm(std::function<void()> fn) { m_onConfirm = std::move(fn); }
    void setOnMove(std::function<void()> fn) { m_onMove = std::move(fn); }
    void dismissImmediately() { hide(); close(); }
    void closeAnimated() { hide(); close(); }
    void showAnimatedCentered(QWidget* host)
    {
        QWidget* owner = host ? host->window() : nullptr;
        if (owner) {
            const QRect r = owner->frameGeometry();
            move(r.center() - QPoint(width() / 2, height() / 2));
        }
        show();
        raise();
        activateWindow();
        PlanningPageSupport::forceReminderWindowOnTop(this);
        QPointer<ScrollReminderDialog> self(this);
        QTimer::singleShot(120, this, [self]() {
            if (!self) return;
            PlanningPageSupport::forceReminderWindowOnTop(self.data());
        });
        auto* eff = new QGraphicsOpacityEffect(this);
        eff->setOpacity(0.0);
        setGraphicsEffect(eff);
        auto* fade = new QPropertyAnimation(eff, "opacity", this);
        fade->setDuration(160);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        fade->start(QAbstractAnimation::DeleteWhenStopped);
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = rect().adjusted(10, 10, -10, -10);
        QPainterPath path; path.addRoundedRect(r, 42, 42);
        QLinearGradient g(r.topLeft(), r.bottomRight());
        g.setColorAt(0.0, QColor(18,24,30,220));
        g.setColorAt(0.25, QColor(84,94,104,170));
        g.setColorAt(0.5, QColor(160,170,180,115));
        g.setColorAt(0.78, QColor(44,52,60,190));
        g.setColorAt(1.0, QColor(14,18,24,226));
        p.fillPath(path, g);
        p.setPen(QPen(QColor(255,255,255,70), 1.0));
        p.drawPath(path);
    }
private:
    QString m_planText;
    QFrame* m_content{};
    std::function<void()> m_onCancel;
    std::function<void()> m_onConfirm;
    std::function<void()> m_onMove;
};
}


PlanningReminderController::PlanningReminderController(PlanningPage* page)
    : m_page(page)
{
}

void PlanningReminderController::setReminderState(const QDate& day, int slotHour, const QString& rawText, const QString& marker)
{
    if (!m_page || !day.isValid()) return;
    QMap<int, QString> dayMap = m_page->m_plan.value(day);
    if (dayMap.isEmpty() && m_page->m_repo) dayMap = m_page->m_repo->visibleDayMap(day);
    const QString current = dayMap.value(slotHour).trimmed();
    const QString basis = !current.isEmpty() ? current : rawText;
    if (basis.trimmed().isEmpty()) return;
    dayMap[slotHour] = normalizePlanningSlotText(withReminderMarker(basis, marker));
    m_page->m_plan[day] = dayMap;
    if (m_page->m_repo) m_page->m_repo->setVisibleDayMap(day, dayMap);
    if (day.year() == m_page->m_year && day.month() == m_page->m_month) m_page->rebuildGrid();
}

bool PlanningReminderController::moveReminderEntry(const QDate& fromDay, int fromHour, const QString& rawText, const QDate& toDay, int toHour)
{
    if (!m_page || !fromDay.isValid() || !toDay.isValid()) return false;
    if (m_page->m_repo && (fromDay != toDay || fromHour != toHour)
        && m_page->m_repo->getDayMap(toDay).contains(toHour)) return false;
    QMap<int, QString> source = m_page->m_plan.value(fromDay);
    if (source.isEmpty() && m_page->m_repo) source = m_page->m_repo->visibleDayMap(fromDay);
    const QString basis = source.value(fromHour).trimmed().isEmpty() ? rawText : source.value(fromHour).trimmed();
    const QString clean = normalizePlanningSlotText(stripReminderMarkers(basis));
    if (clean.isEmpty()) return false;
    source.remove(fromHour);
    if (source.isEmpty()) m_page->m_plan.remove(fromDay); else m_page->m_plan[fromDay] = source;
    if (m_page->m_repo) m_page->m_repo->setVisibleDayMap(fromDay, source);
    QMap<int, QString> dest = m_page->m_plan.value(toDay);
    if (dest.isEmpty() && m_page->m_repo) dest = m_page->m_repo->visibleDayMap(toDay);
    dest[toHour] = clean;
    m_page->m_plan[toDay] = dest;
    if (m_page->m_repo) m_page->m_repo->setVisibleDayMap(toDay, dest);
    if (m_page->m_reminderDay == fromDay || m_page->m_reminderDay == toDay) {
        const QString fromPrefix = QStringLiteral("%1|").arg(fromDay.toString(Qt::ISODate));
        const QString toPrefix = QStringLiteral("%1|").arg(toDay.toString(Qt::ISODate));
        for (auto it = m_page->m_remindedKeys.begin(); it != m_page->m_remindedKeys.end(); ) {
            if (it->startsWith(fromPrefix) || it->startsWith(toPrefix)) it = m_page->m_remindedKeys.erase(it);
            else ++it;
        }
    }
    m_page->rebuildGrid();
    return true;
}

void PlanningReminderController::checkReminders()
{
    if (!m_page || !m_page->window()) return;
    if (m_page->m_activeReminderDialog) return;
    const QDate today = QDate::currentDate();
    if (m_page->m_reminderDay != today) {
        m_page->m_reminderDay = today;
        m_page->m_remindedKeys.clear();
    }

    const QString todayKey = today.toString(Qt::ISODate);
    bool dayReminderActive = m_page->m_reminderActiveDayKeys.contains(todayKey);
    if (!dayReminderActive && m_page->m_repo && m_page->m_repo->isDayReminderActive(today)) {
        m_page->m_reminderActiveDayKeys.insert(todayKey);
        dayReminderActive = true;
    }
    if (!dayReminderActive) return;

    const QDateTime now = QDateTime::currentDateTime();
    if (!m_page->m_plan.contains(today) && m_page->m_repo) {
        const QMap<int, QString> todayMap = m_page->m_repo->visibleDayMap(today);
        if (!todayMap.isEmpty()) m_page->m_plan[today] = todayMap;
    }
    const auto itDay = m_page->m_plan.constFind(today);
    if (itDay == m_page->m_plan.cend()) return;
    struct Hit {
        int hour;
        QString text;
        qint64 secsTo;
        QString key;
    };
    QVector<Hit> hits;
    for (auto it = itDay->cbegin(); it != itDay->cend(); ++it) {
        const int hour = it.key();
        const QString text = it.value().trimmed();
        if (text.isEmpty()) continue;
        if (hasReminderDecisionMarker(text)) continue;
        const QTime reminderTime = extractReminderTime(text, hour);
        const QDateTime scheduled(today, reminderTime);
        if (!scheduled.isValid()) continue;
        const qint64 secsTo = now.secsTo(scheduled);
        const int leadMinutes = GeneralSettingsService::loadPlanningReminderLeadMinutes();
        const qint64 leadSeconds = static_cast<qint64>(leadMinutes) * 60;
        // Do not depend on the Planning tab being visible: once the configured
        // reminder point is reached, show it even if the timer tick happens
        // slightly later. Drop clearly stale reminders after the planned start.
        if (secsTo <= leadSeconds && secsTo >= -5 * 60) {
            const QString key = reminderRuntimeKey(today, hour, text);
            if (m_page->m_remindedKeys.contains(key)) continue;
            hits.push_back({hour, text, secsTo, key});
        }
    }
    if (hits.isEmpty()) return;
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.secsTo != b.secsTo) return a.secsTo < b.secsTo;
        return a.hour < b.hour;
    });
    const Hit hit = hits.first();
    m_page->m_remindedKeys.insert(hit.key);
    m_page->showAnimatedReminder(today, hit.hour, hit.text);
}


void PlanningReminderController::showAnimatedReminder(const QDate& day, int slotHour, const QString& rawText)
{
    if (!m_page) return;
    m_page->dismissActiveReminder(true);
    const QTime shownTime = extractReminderTime(rawText, slotHour);
    QString cleanText = stripReminderMarkers(rawText);
    const QString shownPrefix = shownTime.toString(QStringLiteral("HH:mm"));
    static const QRegularExpression leadingTimeRe(QStringLiteral(R"(^\s*((?:[01]?\d|2[0-3]):[0-5]\d)\s+)"));
    const auto leading = leadingTimeRe.match(cleanText);
    if (leading.hasMatch()) cleanText.remove(0, leading.capturedLength(1));
    const QString text = normalizePlanningSlotText(QStringLiteral("%1 %2").arg(shownPrefix, cleanText.trimmed()));
    auto* dlg = new ScrollReminderDialog(text, m_page->window());
    m_page->m_activeReminderDialog = dlg;
    QObject::connect(dlg, &QObject::destroyed, m_page, [page=m_page, dlg]() { if (page->m_activeReminderDialog == dlg) page->m_activeReminderDialog.clear(); });
    dlg->setOnCancel([page=m_page, day, slotHour, rawText]() { if (page) page->setReminderState(day, slotHour, rawText, QString::fromUtf8("❌")); });
    QPointer<ScrollReminderDialog> safeDlg(dlg);
    dlg->setOnConfirm([page=m_page, day, slotHour, rawText, safeDlg]() {
        if (!page) return;
        const QString plateNorm = extractPlanningPlateNorm(rawText);
        const int sourceOrderNumber = plateNorm.isEmpty() ? -1 : findLatestVehicleOrderNumber(plateNorm);
        const QTime plannedStartTime = extractReminderTime(rawText, slotHour);
        if (page->m_activeReminderDialog == safeDlg) page->m_activeReminderDialog.clear();
        QPointer<PlanningPage> safePage(page);
        QPointer<ScrollReminderDialog> dialogToClose(safeDlg);
        QWidget* owner = page->window();
        QPointer<QWidget> safeOwner(owner);
        QPointer<MainWindow> safeMainWindow(qobject_cast<MainWindow*>(owner));
        QObject* ctx = owner ? static_cast<QObject*>(owner) : static_cast<QObject*>(page);
        QTimer::singleShot(0, ctx, [safePage, dialogToClose, day, slotHour, rawText, plateNorm, sourceOrderNumber, plannedStartTime, safeOwner, safeMainWindow]() {
            if (!safePage) return;
            if (dialogToClose) dialogToClose->dismissImmediately();
            safePage->setReminderState(day, slotHour, rawText, QString::fromUtf8("✅"));
            if (sourceOrderNumber > 0 && safeMainWindow) {
                QTimer::singleShot(180, safeMainWindow, [safeMainWindow, sourceOrderNumber, rawText, day, plannedStartTime]() {
                    if (safeMainWindow) safeMainWindow->requestOpenNewOrderFromPlanning(sourceOrderNumber, rawText, day, plannedStartTime);
                });
                return;
            }
            QObject* fallbackCtx = safeOwner ? static_cast<QObject*>(safeOwner.data()) : static_cast<QObject*>(safePage.data());
            QTimer::singleShot(180, fallbackCtx, [safeOwner, safePage, sourceOrderNumber, rawText, day, plannedStartTime]() {
                QWidget* finalOwner = safeOwner ? safeOwner.data() : static_cast<QWidget*>(safePage.data());
                if (!finalOwner) return;
                BackupPerformanceGuard::hold(AppContext::instance().backupManager(), 2500);
                OrderDialog orderDlg(finalOwner);
                orderDlg.setWindowTitle(QString::fromUtf8("Новий наряд"));
                if (sourceOrderNumber > 0) orderDlg.prefillFromPlanningSource(sourceOrderNumber, rawText, day, plannedStartTime);
                else orderDlg.prefillFromPlanningEntry(rawText, day, plannedStartTime);
                orderDlg.exec();
            });
        });
    });
    dlg->setOnMove([page=m_page, dlg, day, slotHour, rawText]() {
        if (!page) return;
        QDialog moveDlg(dlg);
        moveDlg.setWindowTitle(QString::fromUtf8("Перенести запис"));
        moveDlg.setModal(true);
        moveDlg.setAttribute(Qt::WA_StyledBackground, true);
        moveDlg.setAttribute(Qt::WA_TranslucentBackground, true);
        moveDlg.setStyleSheet(R"QSS(
            QDialog { background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 rgba(18,24,30,220), stop:0.18 rgba(74,84,94,170), stop:0.42 rgba(145,155,165,110), stop:0.72 rgba(50,58,68,185), stop:1 rgba(12,16,22,228)); border: 1px solid rgba(255,255,255,62); border-radius: 24px; }
            QLabel { color: rgba(245,248,252,240); font-size: 15pt; font-weight: 700; background: transparent; }
            QDateEdit, QTimeEdit { min-height: 58px; padding: 0 18px; border-radius: 18px; color: #111111; background: #ffffff; border: 1px solid rgba(22,28,35,0.34); font-size: 16pt; font-weight: 700; selection-background-color: #b8d6ff; selection-color: #111111; }
            QDateEdit::up-button, QDateEdit::down-button, QTimeEdit::up-button, QTimeEdit::down-button, QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { width: 0px; border: none; background: transparent; }
            QPushButton { min-width: 142px; min-height: 54px; border-radius: 18px; padding: 8px 16px; font-size: 14pt; font-weight: 700; color: rgba(248,250,252,240); background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(78,90,102,180), stop:0.55 rgba(40,48,58,190), stop:1 rgba(18,23,30,210)); border: 1px solid rgba(255,255,255,70); }
            QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(92,105,119,195), stop:0.55 rgba(48,58,68,208), stop:1 rgba(22,28,36,220)); }
        )QSS");
        auto* l = new QVBoxLayout(&moveDlg);
        l->setContentsMargins(28, 24, 28, 22);
        l->setSpacing(16);
        auto* row = new QFormLayout();
        row->setHorizontalSpacing(18);
        row->setVerticalSpacing(14);
        row->setLabelAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        row->setFormAlignment(Qt::AlignTop);
        auto* d = new QDateEdit(day, &moveDlg);
        d->setCalendarPopup(false); d->setDisplayFormat(QStringLiteral("dd.MM.yy")); d->setWrapping(true); d->setAccelerated(true); d->setAlignment(Qt::AlignCenter); d->setMinimumWidth(AdaptiveUi::px(d, 220)); d->setFocusPolicy(Qt::StrongFocus); d->setButtonSymbols(QAbstractSpinBox::NoButtons); d->setToolTip(QString::fromUtf8("Клікни по даті й крути колесо миші"));
        auto* t = new QTimeEdit(extractReminderTime(rawText, slotHour), &moveDlg);
        t->setDisplayFormat(QStringLiteral("HH:mm")); t->setTime(extractReminderTime(rawText, slotHour)); t->setWrapping(true); t->setAccelerated(true); t->setAlignment(Qt::AlignCenter); t->setMinimumWidth(AdaptiveUi::px(t, 220)); t->setFocusPolicy(Qt::StrongFocus); t->setButtonSymbols(QAbstractSpinBox::NoButtons); t->setToolTip(QString::fromUtf8("Клікни по часу й крути колесо миші"));
        row->addRow(QString::fromUtf8("Дата:"), d); row->addRow(QString::fromUtf8("Час:"), t); l->addLayout(row);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &moveDlg);
        l->addSpacing(8); l->addWidget(bb);
        QObject::connect(bb, &QDialogButtonBox::accepted, &moveDlg, &QDialog::accept);
        QObject::connect(bb, &QDialogButtonBox::rejected, &moveDlg, &QDialog::reject);
        moveDlg.resize(578, 338);
        if (QWidget* host = dlg->window()) { const QRect hostRect = host->frameGeometry(); moveDlg.move(hostRect.center() - QPoint(moveDlg.width() / 2, moveDlg.height() / 2)); }
        if (moveDlg.exec() != QDialog::Accepted) return;
        if (page->moveReminderEntry(day, slotHour, rawText, d->date(), t->time().hour())) dlg->closeAnimated();
    });
    dlg->showAnimatedCentered(m_page);
    playReminderKnockSound();
}
