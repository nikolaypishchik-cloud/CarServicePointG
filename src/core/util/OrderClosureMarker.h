#pragma once

#include "core/util/DateFormats.h"
#include <QJsonDocument>

namespace OrderClosureMarker {
inline QString label(const QString& status, const QString& closedAt, const QString& mobileState)
{
    const QString normalized = status.trimmed().toLower();
    if (normalized != QStringLiteral("closed") && normalized != QStringLiteral("completed")) return {};
    const auto workflow = QJsonDocument::fromJson(mobileState.toUtf8()).object().value("workflow").toObject();
    if (workflow.value("ready_completed_role").toString() != QStringLiteral("master")
        || workflow.value("ready_completed_source").toString() != QStringLiteral("mobile")
        || workflow.value("ready_completed_by").toString().trimmed().isEmpty()) return {};
    const auto closed = DateFormats::parseDateTime(closedAt);
    const auto marked = DateFormats::parseDateTime(workflow.value("ready_completed_at").toString());
    // An old phone closure must not label a later manual desktop closure.
    if (!closed.isValid() || !marked.isValid() || closed.toSecsSinceEpoch() != marked.toSecsSinceEpoch()) return {};
    return QString::fromUtf8("Закрито майстром");
}
}
