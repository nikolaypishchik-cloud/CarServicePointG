#pragma once

#include <QWidget>
#include <QDate>
#include <QDateTime>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <QVector>
#include <QRect>
#include <QPointer>
class QHideEvent;
class QShowEvent;

class QComboBox;
class QSpinBox;
class QPushButton;
class QScrollArea;
class QGridLayout;
class QLabel;
class QFrame;
class QLineEdit;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QParallelAnimationGroup;
class QDialog;
class QTableWidget;
class PlanningRepository;
class PlanningDesk;
class PlanningDayCell;
class PlanningCalendarController;
class PlanningEditorController;
class PlanningReminderController;
class PlanningPopupController;

class PlanningPage : public QWidget
{
    Q_OBJECT
public:
    explicit PlanningPage(QWidget* parent=nullptr,bool dispatch=false);
    Q_INVOKABLE void setPageFrozen(bool frozen);
    Q_INVOKABLE void openOnlineBookingDate(const QDate& day);
    void refresh();
    void resetSessionView();
    void setDailyView(bool daily);
    void updatePlanningClock(const QDateTime& now);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool event(QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    friend class PlanningCalendarController;
    friend class PlanningEditorController;
    friend class PlanningReminderController;
    friend class PlanningPopupController;
    struct SlotRow {
        QLabel* timeLabel{};
        QLineEdit* edit{};
    };

    void buildUi();
    void rebuildGrid();
    void ensureCellPoolSize(int requiredCount);
    void setMonth(const QDate& anyDayInMonth);
    void alignCurrentWeekTop(bool forceResetToTodayMonth=false);
    void openEditorFor(const QDate& d);
    void closeEditor(bool save);
    void updateEditorHeader();
    void refreshDailyPlanning();
    void fitDailyRows();
    QDateTime m_displayTime;
    QTableWidget* m_dailyPlanning = nullptr;

    // editor animation
    QRect editorTargetRect() const;
    void animateEditorOpen();
    void animateEditorClose(bool hideOnly);

    // hover preview
    void showPreviewFor(const QDate& d);
    void hidePreview();
    void checkReminders();
    void showAnimatedReminder(const QDate& day, int slotHour, const QString& rawText);
    void dismissActiveReminder(bool immediate);
    void setReminderState(const QDate& day, int slotHour, const QString& rawText, const QString& marker);
    bool moveReminderEntry(const QDate& fromDay, int fromHour, const QString& rawText, const QDate& toDay, int toHour);

    PlanningDesk* m_desk=nullptr;
    PlanningRepository* m_repo{};

    PlanningCalendarController* m_calendarController{};
    PlanningEditorController* m_editorController{};
    PlanningReminderController* m_reminderController{};
    PlanningPopupController* m_popupController{};


    // data (cached from DB)
    QMap<QDate, QMap<int, QString>> m_plan; // date -> (hour -> text)

    // current month
    int m_year = 0;
    int m_month = 0;

    // top controls
    QPushButton* m_btnPrev{};
    QPushButton* m_btnNext{};
    QLabel* m_lblMonthTitle{};
    QComboBox* m_cbMonth{};
    QSpinBox* m_spYear{};
    QPushButton* m_btnToday{};

    // calendar grid
    QScrollArea* m_scroll{};
    QWidget* m_gridHost{};
    QGridLayout* m_grid{};
    QWidget* m_bottomSpacer = nullptr;
    QVector<PlanningDayCell*> m_dayCellPool;

    // hover preview popup (tool window)
    QFrame* m_preview{};
    QLabel* m_previewTitle{};
    QLabel* m_previewBody{};
    QLabel* m_previewHint{};
    QTimer m_hoverTimer;
    QDate m_hoverDate;

    // reminders
    QTimer m_reminderTimer;
    QSet<QString> m_remindedKeys;
    QSet<QString> m_reminderActiveDayKeys;
    QDate m_reminderDay;
    QPointer<QDialog> m_activeReminderDialog;
    int m_alignGeneration = 0;
    bool m_pageFrozen = false;

    // editor overlay
    QFrame* m_editor{};
    QLabel* m_editorDate{};
    QPushButton* m_btnClear{};
    QPushButton* m_btnClose{};
    QLabel* m_editorInfo{};
    QVector<SlotRow> m_rows;
    QDate m_editDate;

    // animation helpers (owned by m_editor)
    QGraphicsOpacityEffect* m_editorOpacity{};
    QPropertyAnimation* m_editorGeomAnim{};
    QPropertyAnimation* m_editorOpacityAnim{};
    QParallelAnimationGroup* m_editorAnimGroup{};
};
