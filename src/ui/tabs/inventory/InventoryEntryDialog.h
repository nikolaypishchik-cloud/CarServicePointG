#pragma once

#include <QDialog>
#include <QLabel>
#include <QKeyEvent>
#include <QMouseEvent>

class InventoryEntryDialog final : public QDialog {
public:
    using QDialog::QDialog;
    static QString materialStyleSheet() {
        return QStringLiteral(
            "QDialog {background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #39464d,stop:0.8 #293741,stop:1 #192631);}"
            "QLabel {color:#f2f6fa;background:transparent;}"
            "QWidget#workshopInventoryPurchaseForm {background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #263642,stop:1 #17232d);}"
            "QScrollArea,QScrollArea>QWidget {background:#20303b;border-color:#7298a6;}"
            "QLineEdit,QComboBox,QDateEdit,QDoubleSpinBox {background:#f7f9fb;color:#14212a;border:1px solid #a4b6c2;border-radius:5px;padding:6px;}"
            "QLineEdit:focus,QComboBox:focus,QDateEdit:focus,QDoubleSpinBox:focus {border-color:#28a6bd;}"
            "QComboBox QAbstractItemView {background:#f7f9fb;color:#14212a;selection-background-color:#beeaf0;selection-color:#12242c;}"
            "QDialogButtonBox {background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #263641,stop:1 #111d27);}"
            "QPushButton {color:#f2f6fa;border-color:#8195a4;background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #465560,stop:0.48 #23303b,stop:1 #101820);}"
            "QPushButton#saveMaterialReceipt {border-color:#71d9e8;background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #375663,stop:0.48 #213a46,stop:1 #12232e);}"
            "QPushButton:hover,QPushButton:focus {border-color:#95e4ef;} QPushButton:disabled {color:#97a4ad;}");
    }
    void enableHeaderDrag(QWidget* header) {
        header->installEventFilter(this);
        for(auto* label:header->findChildren<QLabel*>())label->installEventFilter(this);
    }
protected:
    bool eventFilter(QObject* watched,QEvent* event) override {
        if(event->type()==QEvent::MouseButtonPress){
            auto* mouse=static_cast<QMouseEvent*>(event);
            if(mouse->button()==Qt::LeftButton){m_dragging=true;m_dragOffset=mouse->globalPosition().toPoint()-pos();return true;}
        }else if(event->type()==QEvent::MouseMove&&m_dragging){
            move(static_cast<QMouseEvent*>(event)->globalPosition().toPoint()-m_dragOffset);return true;
        }else if(event->type()==QEvent::MouseButtonRelease&&m_dragging){m_dragging=false;return true;}
        return QDialog::eventFilter(watched,event);
    }
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key()==Qt::Key_Return || event->key()==Qt::Key_Enter) {
            event->accept();
            return;
        }
        QDialog::keyPressEvent(event);
    }
private:
    bool m_dragging=false;
    QPoint m_dragOffset;
};

