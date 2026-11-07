#pragma once

#include <QRect>

namespace MainWindowSupport {

QRect clampNormalWindowFrameToAvailableGeometry(const QRect& currentFrame,
                                                const QRect& currentGeometry,
                                                const QRect& availableGeometry);

} // namespace MainWindowSupport
