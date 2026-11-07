#pragma once
#include <QObject>
#include <QPointer>
#include <QHash>
#include <QList>
#include <QLineEdit>
#include <QComboBox>
#include <QAbstractSpinBox>
#include <QDoubleSpinBox>
#include <QDateTimeEdit>
#include <QCalendarWidget>
#include <QMouseEvent>
#include <QFocusEvent>
#include <QStyleOptionSpinBox>
#include <QStyle>
#include <QTimer>
#include "DraftDateEdit.h"

// Opt-in for a draft-entry form, never installed on the whole application.
// A cleared spin box retains an internal Qt value until input is committed;
// keep an explicit pending flag so Save cannot silently reuse that old value.
class ClearOnEntryFilter final : public QObject {
public:
    explicit ClearOnEntryFilter(QObject* parent):QObject(parent){}
    void attach(QWidget* field,const QString& label,bool optional=false) {
        m_fields.append(field);
        field->setProperty("entryLabel",label);
        field->setProperty("entryOptional",optional);
        watch(field,field);
        QLineEdit* line=qobject_cast<QLineEdit*>(field);
        if(auto* spin=qobject_cast<QAbstractSpinBox*>(field))line=spin->findChild<QLineEdit*>();
        if(auto* combo=qobject_cast<QComboBox*>(field)) {
            line=combo->lineEdit();
            connect(combo,qOverload<int>(&QComboBox::currentIndexChanged),this,[field](int index){
                if(index>=0)field->setProperty("entryPending",false);
            });
        }
        if(line) {
            watch(line,field);
            connect(line,&QLineEdit::textEdited,this,[field,line]{
                field->setProperty("entryPending",line->text().trimmed().isEmpty() || !line->hasAcceptableInput());
            });
        }
        if(auto* spin=qobject_cast<QDoubleSpinBox*>(field))
            connect(spin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[field]{field->setProperty("entryPending",false);});
        if(auto* date=qobject_cast<QDateTimeEdit*>(field)) {
            connect(date,&QDateTimeEdit::dateTimeChanged,this,[field]{field->setProperty("entryPending",false);});
            if(date->calendarPopup())connect(date->calendarWidget(),&QCalendarWidget::clicked,this,[field]{field->setProperty("entryPending",false);});
        }
        if(auto* date=dynamic_cast<DraftDateEdit*>(field))watch(date->entryEditor(),field);
    }
    bool empty(QWidget* field) const {return field->property("entryPending").toBool();}
    QWidget* firstIncomplete() const {
        for(const auto& field:m_fields) {
            if(!field || !field->isEnabled())continue;
            if(field->property("entryOptional").toBool()) {
                auto* date=dynamic_cast<DraftDateEdit*>(field.data());
                if(!date || date->entryEditor()->text().trimmed().isEmpty() || !empty(field))continue;
            }
            if(qobject_cast<QAbstractSpinBox*>(field) && empty(field))return field;
            if(auto* combo=qobject_cast<QComboBox*>(field))if(combo->currentIndex()<0)return field;
        }
        return nullptr;
    }
protected:
    bool eventFilter(QObject* object,QEvent* event) override {
        auto field=m_targets.value(object);if(!field || !field->isEnabled())return false;
        if(event->type()==QEvent::FocusOut) {
            if(static_cast<QFocusEvent*>(event)->reason()!=Qt::PopupFocusReason) {
                if(m_active==field)m_active=nullptr;
                if(auto* spin=qobject_cast<QAbstractSpinBox*>(field.data());spin && !dynamic_cast<DraftDateEdit*>(spin) && empty(field))
                    QTimer::singleShot(0,spin,[spin]{if(spin->property("entryPending").toBool())spin->clear();});
            }
            return false;
        }
        if(event->type()!=QEvent::MouseButtonPress)return false;
        const auto* mouse=static_cast<QMouseEvent*>(event);
        if(mouse->button()!=Qt::LeftButton || m_active==field)return false;
        if(auto* spin=qobject_cast<QAbstractSpinBox*>(field.data())) {
            if(spin->isReadOnly())return false;
            auto* target=qobject_cast<QWidget*>(object);
            const QPoint point=target->mapTo(field,mouse->position().toPoint());
            QStyleOptionSpinBox option;option.initFrom(spin);option.rect=spin->rect();option.frame=spin->hasFrame();option.buttonSymbols=spin->buttonSymbols();
            if(!spin->style()->subControlRect(QStyle::CC_SpinBox,&option,QStyle::SC_SpinBoxEditField,spin).contains(point))return false;
            if(auto* date=dynamic_cast<DraftDateEdit*>(spin)) {
                date->clear();m_active=field;return true;
            }
            m_active=field;spin->setFocus(Qt::MouseFocusReason);
            spin->clear();
            // QDateTimeEdit handles the same press by selecting a section and
            // restoring its cached date. Consume only this first edit-area press.
            if(auto* line=spin->findChild<QLineEdit*>()){line->clear();line->setCursorPosition(0);}
            field->setProperty("entryPending",true);
            return true;
        } else if(auto* line=qobject_cast<QLineEdit*>(field.data())) {
            if(line->isReadOnly())return false;
            m_active=field;line->clear();
        } else if(auto* combo=qobject_cast<QComboBox*>(field.data())) {
            m_active=field;combo->setCurrentIndex(-1);
            if(combo->isEditable())combo->clearEditText();
        }
        return false;
    }
private:
    void watch(QObject* target,QWidget* field){m_targets.insert(target,field);target->installEventFilter(this);}
    QHash<QObject*,QPointer<QWidget>> m_targets;
    QList<QPointer<QWidget>> m_fields;
    QPointer<QWidget> m_active;
};
