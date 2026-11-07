#pragma once
#include <QDateEdit>
#include <QLineEdit>
#include <QCalendarWidget>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QApplication>

// QDateEdit edits sections of a cached date; a plain editor lets a new draft
// start truly empty without interpreting incomplete input as that cached date.
class DraftDateEdit final : public QDateEdit {
public:
    explicit DraftDateEdit(QDate value,QWidget* parent=nullptr):QDateEdit(value,parent) {
        setCalendarPopup(true);
        m_draft=new QLineEdit(this);m_draft->setFrame(false);m_draft->hide();
        m_draft->setObjectName("draftDateInput");
        connect(m_draft,&QLineEdit::textChanged,this,[this]{
            const auto parsed=parse(m_draft->text());
            const bool valid=parsed.isValid() && parsed>=minimumDate() && parsed<=maximumDate();
            setProperty("entryPending",!valid);
            if(valid) {m_applying=true;QDateEdit::setDate(parsed);m_applying=false;}
        });
        connect(this,&QDateEdit::dateChanged,this,[this]{if(!m_applying)finishDraft();});
        connect(calendarWidget(),&QCalendarWidget::clicked,this,[this]{finishDraft();});
    }
    QLineEdit* entryEditor() const {return m_draft;}
    void clear() override {
        m_draft->setGeometry(lineEdit()->geometry());
        m_draft->setFont(lineEdit()->font());m_draft->setPalette(lineEdit()->palette());
        m_draft->clear();m_draft->show();m_draft->raise();
        m_draft->setFocus(Qt::MouseFocusReason);setProperty("entryPending",true);
    }
    void setDate(QDate value) {QDateEdit::setDate(value);finishDraft();}
protected:
    void resizeEvent(QResizeEvent* event) override {
        QDateEdit::resizeEvent(event);m_draft->setGeometry(lineEdit()->geometry());
    }
    void keyPressEvent(QKeyEvent* event) override {
        if(m_draft->isVisible())QApplication::sendEvent(m_draft,event);
        else QDateEdit::keyPressEvent(event);
    }
private:
    static QDate parse(const QString& text) {
        static const QRegularExpression format(QStringLiteral("^(\\d{1,2})[./-](\\d{1,2})[./-](\\d{2}|\\d{4})$"));
        const auto match=format.match(text.trimmed());if(!match.hasMatch())return {};
        int year=match.captured(3).toInt();if(match.capturedLength(3)==2)year+=2000;
        return QDate(year,match.captured(2).toInt(),match.captured(1).toInt());
    }
    void finishDraft() {
        if(m_draft->hasFocus())setFocus(Qt::OtherFocusReason);
        m_draft->hide();setProperty("entryPending",false);
    }
    QLineEdit* m_draft=nullptr;
    bool m_applying=false;
};
