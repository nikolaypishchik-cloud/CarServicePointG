#pragma once
#include <QCalendarWidget>
#include <QDateEdit>
#include <QEvent>
#include <QSignalBlocker>
#include <QTimer>

// The minimum date represents an empty optional field, not a useful calendar page.
class OptionalDatePopup final : public QObject {
public:
    explicit OptionalDatePopup(QDateEdit* editor) : QObject(editor), m_editor(editor) {
        editor->setCalendarPopup(true);
        editor->calendarWidget()->installEventFilter(this);
    }
protected:
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()==QEvent::Show) {
            QTimer::singleShot(0,this,[this] {
                auto* calendar=m_editor->calendarWidget();
                if(!calendar->isVisible() || m_editor->date()!=m_editor->minimumDate())return;
                const QSignalBlocker blocked(calendar);
                calendar->setSelectedDate(QDate::currentDate());
                calendar->showSelectedDate();
            });
        }
        return QObject::eventFilter(object,event);
    }
private:
    QDateEdit* m_editor;
};
