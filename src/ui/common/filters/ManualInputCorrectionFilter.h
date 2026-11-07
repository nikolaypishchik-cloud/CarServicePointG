#pragma once
#include <QObject>
#include "core/util/ManualInputNormalizer.h"
class QWidget;
class QMenu;
class ManualInputCorrectionFilter : public QObject {
public:
    explicit ManualInputCorrectionFilter(QObject* parent=nullptr);
    static ManualInputNormalizer::Role roleFor(QWidget* editor);
    static void appendMenu(QMenu& menu,QWidget* editor);
    static bool correct(QWidget* editor,bool explicitRequest=false);
protected:
    bool eventFilter(QObject* watched,QEvent* event) override;
private:
    void observe(QWidget* editor);
};
