#include "ui/main/MainWindowSupport.h"

#include <QtGlobal>

namespace MainWindowSupport {

QRect clampNormalWindowFrameToAvailableGeometry(const QRect& currentFrame,
                                                const QRect& currentGeometry,
                                                const QRect& availableGeometry)
{
    QRect targetFrame = currentFrame;
    targetFrame.setWidth(qMin(targetFrame.width(), availableGeometry.width()));
    targetFrame.setHeight(qMin(targetFrame.height(), availableGeometry.height()));
    if (targetFrame.left() < availableGeometry.left()) targetFrame.moveLeft(availableGeometry.left());
    if (targetFrame.top() < availableGeometry.top()) targetFrame.moveTop(availableGeometry.top());
    if (targetFrame.right() > availableGeometry.right()) targetFrame.moveRight(availableGeometry.right());
    if (targetFrame.bottom() > availableGeometry.bottom()) targetFrame.moveBottom(availableGeometry.bottom());

    const int frameLeft = qMax(0, currentGeometry.left() - currentFrame.left());
    const int frameTop = qMax(0, currentGeometry.top() - currentFrame.top());
    const int frameRight = qMax(0, currentFrame.right() - currentGeometry.right());
    const int frameBottom = qMax(0, currentFrame.bottom() - currentGeometry.bottom());
    return targetFrame.adjusted(frameLeft, frameTop, -frameRight, -frameBottom);
}

} // namespace MainWindowSupport
