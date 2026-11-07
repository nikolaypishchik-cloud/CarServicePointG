#pragma once

#include <QDate>
#include <QFrame>

class QLabel;
class QAbstractButton;

class PlanningDayCell final : public QFrame
{
    Q_OBJECT
public:
    explicit PlanningDayCell(QWidget* parent=nullptr);

    void setDate(const QDate& date);
    QDate date() const;

    void setSelected(bool on);
    void setCurrentDate(bool on);
    void setPreviewText(const QString& text);
    void setReminderActive(bool on);
    bool reminderActive() const;
    void resetVisualState();

signals:
    void reminderBellToggled(const QDate& date, bool active);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void refreshText();
    void refreshBell();
    void repolish();

    QDate m_date;
    QLabel* m_header = nullptr;
    QLabel* m_body = nullptr;
    QAbstractButton* m_bell = nullptr;
    bool m_selected = false;
    bool m_current = false;
    bool m_reminderActive = false;
};
