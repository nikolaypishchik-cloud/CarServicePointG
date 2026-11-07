#pragma once

#include <QDialog>
#include <QLabel>
#include <QKeyEvent>
#include <QMouseEvent>

class InventoryEntryDialog final : public QDialog {
public:
    using QDialog::QDialog;
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

