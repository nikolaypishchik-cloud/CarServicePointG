#include "ManualInputCorrectionFilter.h"
#include <QApplication>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QFileDialog>
#include <QFormLayout>
#include <QBoxLayout>
#include <QGridLayout>
#include <QInputDialog>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QRegularExpression>
#include <QSettings>
#include <QTextEdit>
#include <QTimer>
#include <QToolTip>
#include <QValidator>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
using Role=ManualInputNormalizer::Role;
bool applying=false;
bool enabled(){return QSettings().value("inputCorrection/enabled",true).toBool();}
QWidget* editorWidget(QObject* o) {
    auto* w=qobject_cast<QWidget*>(o);
    if(auto* combo=qobject_cast<QComboBox*>(w))return combo->isEditable()?combo->lineEdit():nullptr;
    if(qobject_cast<QLineEdit*>(w) || qobject_cast<QTextEdit*>(w) || qobject_cast<QPlainTextEdit*>(w))return w;
    if(w && (qobject_cast<QTextEdit*>(w->parentWidget()) || qobject_cast<QPlainTextEdit*>(w->parentWidget())))return w->parentWidget();
    return nullptr;
}
bool editable(QWidget* w) {
    if(auto* e=qobject_cast<QLineEdit*>(w))return !e->isReadOnly() && e->echoMode()==QLineEdit::Normal;
    if(auto* e=qobject_cast<QTextEdit*>(w))return !e->isReadOnly();
    if(auto* e=qobject_cast<QPlainTextEdit*>(w))return !e->isReadOnly();return false;
}
QString text(QWidget* w) {
    if(auto* e=qobject_cast<QLineEdit*>(w))return e->text();
    if(auto* e=qobject_cast<QTextEdit*>(w))return e->toPlainText();
    if(auto* e=qobject_cast<QPlainTextEdit*>(w))return e->toPlainText();return {};
}
QTextCursor cursor(QWidget* w) {
    if(auto* e=qobject_cast<QTextEdit*>(w))return e->textCursor();
    if(auto* e=qobject_cast<QPlainTextEdit*>(w))return e->textCursor();return {};
}
void setCursor(QWidget* w,const QTextCursor& c) {
    if(auto* e=qobject_cast<QTextEdit*>(w))e->setTextCursor(c);
    if(auto* e=qobject_cast<QPlainTextEdit*>(w))e->setTextCursor(c);
}
void replace(QWidget* w,const QString& value) {
    const auto old=text(w);if(old==value)return;
    int prefix=0,suffix=0;while(prefix<old.size() && prefix<value.size() && old[prefix]==value[prefix])++prefix;
    while(suffix<old.size()-prefix && suffix<value.size()-prefix && old[old.size()-suffix-1]==value[value.size()-suffix-1])++suffix;
    auto remap=[&](int p){return p<=prefix?p:p>=old.size()-suffix?p+int(value.size()-old.size()):qMin(prefix+int(value.size()-prefix-suffix),p);};
    applying=true;
    if(auto* e=qobject_cast<QLineEdit*>(w)){
        const int position=e->cursorPosition(),start=e->selectionStart(),length=e->selectedText().size();
        e->setSelection(prefix,old.size()-prefix-suffix);e->insert(value.mid(prefix,value.size()-prefix-suffix));
        if(start>=0)e->setSelection(remap(start),remap(start+length)-remap(start));else e->setCursorPosition(remap(position));
    } else {
        auto c=cursor(w);const int position=c.position(),anchor=c.anchor();
        c.beginEditBlock();c.setPosition(prefix);c.setPosition(old.size()-suffix,QTextCursor::KeepAnchor);c.insertText(value.mid(prefix,value.size()-prefix-suffix));c.endEditBlock();
        c.setPosition(remap(anchor));c.setPosition(remap(position),QTextCursor::KeepAnchor);setCursor(w,c);
    }
    applying=false;
}
QString hint(QWidget* w) {
    QWidget* field=w;
    if(qobject_cast<QComboBox*>(w->parentWidget()))field=w->parentWidget();
    QString result=field->objectName()+" "+field->accessibleName();
    if(auto* e=qobject_cast<QLineEdit*>(w))result+=" "+e->placeholderText();
    if(auto* c=qobject_cast<QComboBox*>(field))result+=" "+c->placeholderText();
    for(QWidget* p=field->parentWidget();p;p=p->parentWidget()) {
        if(auto* view=qobject_cast<QAbstractItemView*>(p)) {
            if(view->model() && view->currentIndex().isValid())result+=" "+view->model()->headerData(view->currentIndex().column(),Qt::Horizontal).toString();
            break;
        }
    }
    if(auto* parent=field->parentWidget()) {
        for(auto* label:parent->findChildren<QLabel*>(QString(),Qt::FindDirectChildrenOnly))if(label->buddy()==field)result+=" "+label->text();
        const auto layouts=parent->findChildren<QLayout*>();
        for(auto* layout:layouts) {
            if(auto* box=qobject_cast<QBoxLayout*>(layout)) {
                const int i=box->indexOf(field);
                if(i>0)if(auto* label=qobject_cast<QLabel*>(box->itemAt(i-1)->widget()))result+=" "+label->text();
            }
            if(auto* form=qobject_cast<QFormLayout*>(layout))if(auto* label=qobject_cast<QLabel*>(form->labelForField(field)))result+=" "+label->text();
            if(auto* grid=qobject_cast<QGridLayout*>(layout)) {
                const int i=grid->indexOf(field);if(i<0)continue;int row,col,rs,cs;grid->getItemPosition(i,&row,&col,&rs,&cs);
                auto labelAt=[&](int r,int c){auto* item=r>=0 && c>=0?grid->itemAtPosition(r,c):nullptr;return item?qobject_cast<QLabel*>(item->widget()):nullptr;};
                if(auto* l=labelAt(row,col-1))result+=" "+l->text();else if(auto* l=labelAt(row-1,col))result+=" "+l->text();
            }
        }
    }
    for(QWidget* p=field;p;p=p->parentWidget())if(auto* d=qobject_cast<QInputDialog*>(p)){result+=" "+d->labelText();break;}
    return result;
}
QMap<QString,QString> custom(Role role) {
    const auto json=QJsonDocument::fromJson(QSettings().value("inputCorrection/dictionary/"+ManualInputNormalizer::roleKey(role)).toByteArray()).object();
    QMap<QString,QString> result;for(auto it=json.begin();it!=json.end();++it)if(it.value().isString())result[it.key()]=it.value().toString();return result;
}
void remember(Role role,const QString& from,const QString& to) {
    auto rules=custom(role);if(to.isEmpty())rules.remove(from);else rules[from]=to;
    QJsonObject json;for(auto it=rules.begin();it!=rules.end();++it)json[it.key()]=it.value();
    QSettings().setValue("inputCorrection/dictionary/"+ManualInputNormalizer::roleKey(role),QJsonDocument(json).toJson(QJsonDocument::Compact));
}
}
ManualInputCorrectionFilter::ManualInputCorrectionFilter(QObject* parent):QObject(parent){}
ManualInputNormalizer::Role ManualInputCorrectionFilter::roleFor(QWidget* w) {
    if(!w || !editable(w))return Role::Protected;
    for(QWidget* p=w;p;p=p->parentWidget())if(qobject_cast<QAbstractSpinBox*>(p) || qobject_cast<QFileDialog*>(p))return Role::Protected;
    auto explicitRole=w->property("inputCorrectionRole").toString();
    if(explicitRole.isEmpty() && qobject_cast<QComboBox*>(w->parentWidget()))explicitRole=w->parentWidget()->property("inputCorrectionRole").toString();
    if(!explicitRole.isEmpty())for(const auto r:{Role::Protected,Role::Text,Role::Technical,Role::Vehicle,Role::Plate,Role::Date,Role::Search,Role::Planning})if(ManualInputNormalizer::roleKey(r)==explicitRole)return r;
    if(auto* e=qobject_cast<QLineEdit*>(w))if(qobject_cast<const QIntValidator*>(e->validator()) || qobject_cast<const QDoubleValidator*>(e->validator()))return Role::Protected;
    return ManualInputNormalizer::roleForHint(hint(w));
}
bool ManualInputCorrectionFilter::correct(QWidget* w,bool force) {
    if(!w || applying || !editable(w) || w->property("inputCorrectionDisabled").toBool() || w->property("inputCorrectionComposing").toBool())return false;
    if(!force && (!enabled() || !w->property("inputCorrectionDirty").toBool() || w->property("inputCorrectionSuppressed").toBool()))return false;
    const auto original=text(w);auto role=roleFor(w);if(role==Role::Protected || role==Role::Search)return false;
    const auto result=ManualInputNormalizer::normalize(original,role,custom(role));w->setProperty("inputCorrectionDirty",false);
    if(result.text==original){if(force && !result.warning.isEmpty())QToolTip::showText(w->mapToGlobal(QPoint(0,w->height())),result.warning,w);return false;}
    w->setProperty("inputCorrectionBefore",original);w->setProperty("inputCorrectionAfter",result.text);w->setProperty("inputCorrectionSuppressed",false);
    replace(w,result.text);
    // The explanation is accessible without displaying a popup on every keystroke.
    w->setProperty("inputCorrectionExplanation",QString::fromUtf8("Автовиправлення: %1 → %2. Ctrl+Z — скасувати.").arg(original.left(120),result.text.left(120)));
    return true;
}
void ManualInputCorrectionFilter::observe(QWidget* w) {
    if(!w || w->property("inputCorrectionObserved").toBool())return;
    w->setProperty("inputCorrectionObserved",true);
    auto edited=[w]{
        if(applying)return;
        if(w->property("inputCorrectionSuppressed").toBool())return;
        w->setProperty("inputCorrectionDirty",true);
        // Only completed words at the end of an edit: never change a half-typed model.
        const auto value=text(w);bool atEnd=true;
        if(auto* e=qobject_cast<QLineEdit*>(w))atEnd=e->cursorPosition()==value.size() && !e->hasSelectedText();
        else atEnd=cursor(w).position()==value.size() && !cursor(w).hasSelection();
        if(atEnd && !value.isEmpty() && (value.back().isSpace() || QString(",;!?").contains(value.back())))
            QTimer::singleShot(0,w,[w]{correct(w);});
    };
    if(auto* e=qobject_cast<QLineEdit*>(w)) {
        // Qt emits textEdited before textChanged for a user edit. Track its
        // value so a later programmatic setText clears the manual marker.
        connect(e,&QLineEdit::textEdited,w,[w,edited](const QString& value){
            w->setProperty("inputCorrectionLastEditedText",value);edited();
        });
        connect(e,&QLineEdit::textChanged,w,[w](const QString& value){
            if(!applying && value!=w->property("inputCorrectionLastEditedText").toString())
                w->setProperty("inputCorrectionDirty",false);
        });
    }
    else {
        auto* document=qobject_cast<QTextEdit*>(w)?qobject_cast<QTextEdit*>(w)->document():qobject_cast<QPlainTextEdit*>(w)->document();
        connect(document,&QTextDocument::contentsChange,w,[w,edited](int,int,int){if(w->hasFocus() && w->property("inputCorrectionUserEvent").toBool())edited();});
    }
}
bool ManualInputCorrectionFilter::eventFilter(QObject* watched,QEvent* event) {
    if(applying)return false;
    if(event->type()==QEvent::Show || event->type()==QEvent::FocusIn) {
        if(auto* date=qobject_cast<QDateTimeEdit*>(watched)) {
            if(!date->property("inputCorrectionKeepDateFormat").toBool()) {
                auto format=date->displayFormat();
                if(format.contains('d') && format.contains('M') && format.contains('y')) {
                    format.replace(QRegularExpression("(?:d{1,2}[^a-zA-Z]*M{1,2}[^a-zA-Z]*y{2,4}|y{2,4}[^a-zA-Z]*M{1,2}[^a-zA-Z]*d{1,2})"),"dd.MM.yy");
                    if(format!=date->displayFormat())date->setDisplayFormat(format);
                }
            }
        }
        if(auto* w=editorWidget(watched))observe(w);
    }
    auto* w=editorWidget(watched);if(!w)return false;
    if(event->type()==QEvent::KeyPress) {
        observe(w);auto* key=static_cast<QKeyEvent*>(event);
        if(key->matches(QKeySequence::Undo) || key->matches(QKeySequence::Redo)){w->setProperty("inputCorrectionSuppressed",true);return false;}
        if(key->key()==Qt::Key_F7){correct(w,true);return true;}
        if(key->key()==Qt::Key_Return || key->key()==Qt::Key_Enter || key->key()==Qt::Key_Tab)correct(w);
        if(!key->text().isEmpty() && !(key->modifiers()&Qt::ControlModifier)){w->setProperty("inputCorrectionSuppressed",false);w->setProperty("inputCorrectionUserEvent",true);QTimer::singleShot(0,w,[w]{w->setProperty("inputCorrectionUserEvent",false);});}
        if(key->matches(QKeySequence::Paste)){w->setProperty("inputCorrectionDirty",true);w->setProperty("inputCorrectionSuppressed",false);}
    } else if(event->type()==QEvent::InputMethod) {
        observe(w);auto* input=static_cast<QInputMethodEvent*>(event);w->setProperty("inputCorrectionComposing",!input->preeditString().isEmpty());
        if(!input->commitString().isEmpty()){w->setProperty("inputCorrectionDirty",true);w->setProperty("inputCorrectionSuppressed",false);}
    } else if(event->type()==QEvent::Drop) {
        observe(w);w->setProperty("inputCorrectionDirty",true);w->setProperty("inputCorrectionSuppressed",false);
    } else if(event->type()==QEvent::FocusOut) {
        if(static_cast<QFocusEvent*>(event)->reason()!=Qt::PopupFocusReason)correct(w);
    }
    return false;
}
void ManualInputCorrectionFilter::appendMenu(QMenu& menu,QWidget* w) {
    if(!w || !editable(w) || roleFor(w)==Role::Protected)return;
    const auto role=roleFor(w);auto* sub=menu.addMenu(QString::fromUtf8("Автовиправлення"));
    sub->addAction(QString::fromUtf8("Виправити зараз (F7)"),w,[w]{correct(w,true);});
    const auto before=w->property("inputCorrectionBefore").toString(),after=w->property("inputCorrectionAfter").toString();
    if(!after.isEmpty() && text(w)==after){auto* undo=sub->addAction(QString::fromUtf8("Скасувати останнє автовиправлення"),w,[w,before]{replace(w,before);w->setProperty("inputCorrectionSuppressed",true);w->setProperty("inputCorrectionDirty",false);});undo->setToolTip(w->property("inputCorrectionExplanation").toString());}
    int start=0,length=0;
    if(auto* e=qobject_cast<QLineEdit*>(w)){start=e->hasSelectedText()?e->selectionStart():e->cursorPosition();length=e->selectedText().size();}
    else {const auto c=cursor(w);start=c.selectionStart();length=c.selectionEnd()-start;}
    const auto current=text(w);
    if(length==0){int end=start;while(start>0 && current[start-1].isLetter())--start;while(end<current.size() && current[end].isLetter())++end;length=end-start;}
    const auto selected=current.mid(start,length);
    auto applySelected=[w,current,start,length](const QString& replacement){if(text(w)!=current)return;auto value=current;value.replace(start,length,replacement);w->setProperty("inputCorrectionBefore",current);w->setProperty("inputCorrectionAfter",value);replace(w,value);};
    auto choices=ManualInputNormalizer::suggestions(selected,role);
    if(role==Role::Text || role==Role::Technical)choices.append(ManualInputNormalizer::systemSuggestions(selected));choices.removeDuplicates();
    if(!choices.isEmpty()){sub->addSeparator();for(const auto& choice:choices.mid(0,8))sub->addAction(choice,w,[applySelected,choice]{applySelected(choice);});}
    if(!selected.trimmed().isEmpty() && selected.size()<=80 && role!=Role::Plate && role!=Role::Date && role!=Role::Search) {
        sub->addSeparator();sub->addAction(QString::fromUtf8("Запам’ятати виправлення «%1»…").arg(selected.left(35)),w,[w,selected,role,applySelected]{
            QInputDialog dialog(w);dialog.setWindowTitle(QString::fromUtf8("Особистий словник"));
            dialog.setLabelText(QString::fromUtf8("Правильне написання для «%1»:").arg(selected));dialog.setTextValue(selected);
            for(auto* edit:dialog.findChildren<QLineEdit*>())edit->setProperty("inputCorrectionRole","protected");
            if(dialog.exec()!=QDialog::Accepted)return;
            const auto value=dialog.textValue().trimmed();
            if(!value.isEmpty() && value.size()<=120){remember(role,selected,value);applySelected(value);}
        });
        if(custom(role).contains(selected))sub->addAction(QString::fromUtf8("Забути це особисте правило"),w,[role,selected]{remember(role,selected,{});});
    }
    sub->addSeparator();auto* field=sub->addAction(QString::fromUtf8("Автовиправлення цього поля"));field->setCheckable(true);field->setChecked(!w->property("inputCorrectionDisabled").toBool());connect(field,&QAction::toggled,w,[w](bool on){w->setProperty("inputCorrectionDisabled",!on);});
    auto* global=sub->addAction(QString::fromUtf8("Автовиправлення в усьому застосунку"));global->setCheckable(true);global->setChecked(enabled());connect(global,&QAction::toggled,w,[](bool on){QSettings().setValue("inputCorrection/enabled",on);});
}
