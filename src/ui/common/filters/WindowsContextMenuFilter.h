#pragma once

#include <QObject>

class WindowsContextMenuFilter : public QObject
{
    Q_OBJECT
public:
    explicit WindowsContextMenuFilter(QObject* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool handleForLineEdit(QObject* watched, class QEvent* event);
    bool handleForTextEdit(QObject* watched, class QEvent* event);
    bool handleForItemView(QObject* watched, class QEvent* event);
};
