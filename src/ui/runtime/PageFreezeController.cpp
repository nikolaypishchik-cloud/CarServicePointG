#include "ui/runtime/PageFreezeController.h"

#include "ui/common/widgets/GraphiteShimmerOverlay.h"

#include <QMetaObject>
#include <QStackedWidget>
#include <QWidget>
#include <QVariant>

namespace {
void setFrozenState(QWidget* widget, bool frozen)
{
    if (!widget) return;
    if (widget->property("cs.pageFrozen").toBool() == frozen) return;

    if (frozen) {
        widget->setProperty("cs.prevUpdatesEnabled", widget->updatesEnabled());
        widget->setUpdatesEnabled(false);
    } else {
        const QVariant prevUpdates = widget->property("cs.prevUpdatesEnabled");
        if (prevUpdates.isValid()) widget->setUpdatesEnabled(prevUpdates.toBool());
        else widget->setUpdatesEnabled(true);
    }
    widget->setProperty("cs.pageFrozen", frozen);
}

void notifyPageFreezeState(QWidget* page, bool frozen)
{
    if (!page) return;
    if (page->metaObject()->indexOfMethod("setPageFrozen(bool)") < 0) return;
    QMetaObject::invokeMethod(page, "setPageFrozen", Qt::DirectConnection, Q_ARG(bool, frozen));
}
}

void PageFreezeController::freeze(QWidget* page)
{
    if (!page) return;
    if (page->property("cs.pageFrozen").toBool()) return;
    setFrozenState(page, true);
    notifyPageFreezeState(page, true);
    const auto shimmers = page->findChildren<GraphiteShimmerOverlay*>();
    for (auto* shimmer : shimmers) {
        if (!shimmer) continue;
        shimmer->setProperty("cs.wasVisibleBeforeFreeze", shimmer->isVisible());
        shimmer->hide();
    }
}

void PageFreezeController::thaw(QWidget* page)
{
    if (!page) return;
    if (!page->property("cs.pageFrozen").toBool()) return;
    setFrozenState(page, false);
    notifyPageFreezeState(page, false);
    const auto shimmers = page->findChildren<GraphiteShimmerOverlay*>();
    for (auto* shimmer : shimmers) {
        if (!shimmer) continue;
        if (shimmer->property("cs.wasVisibleBeforeFreeze").toBool()) {
            shimmer->show();
        }
    }
}

void PageFreezeController::apply(QStackedWidget* stack, int activeIndex)
{
    if (!stack) return;
    for (int i = 0; i < stack->count(); ++i) {
        QWidget* page = stack->widget(i);
        if (!page) continue;
        if (i == activeIndex) thaw(page);
        else freeze(page);
    }
}
